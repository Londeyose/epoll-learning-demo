#include "WebServer.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <iostream>

#include "LogMacro.h"

namespace {
constexpr int MAX_EVENTS = 1024;
constexpr uint32_t kListenEvent = EPOLLIN | EPOLLET;
constexpr uint32_t kReadEvent = EPOLLIN | EPOLLRDHUP | EPOLLET;
constexpr uint32_t kWriteEvent = EPOLLOUT | EPOLLRDHUP | EPOLLET;
constexpr uint32_t kProcessEvent = EPOLLRDHUP | EPOLLET;
constexpr uint32_t kWakeupEvent = EPOLLIN | EPOLLET;

int setNonBlocking(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }
}  // namespace

WebServer::WebServer(int port, int trig_mode, int timeout_ms, int thread_num)
    : port_(port),
      timeout_ms_(timeout_ms),
      thread_num_(thread_num),
      server_fd_(-1),
      wakeup_fd_(-1),
      is_running_(false),
      thread_pool_(thread_num_),
      epoller_(MAX_EVENTS) {
    (void)trig_mode;
}

WebServer::~WebServer() {
    if (server_fd_ != -1) {
        ::close(server_fd_);
    }
    if (wakeup_fd_ != -1) {
        ::close(wakeup_fd_);
    }
}

bool WebServer::init() { return initListenSocket(); }

void WebServer::start() {
    if (!init()) {
        std::cerr << "WebServer init failed!" << std::endl;
        return;
    }
    is_running_ = true;
    eventLoop();
}

bool WebServer::initListenSocket() {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ < 0) {
        LOG_ERROR("Fail to create wakeup fd.");
        return false;
    }
    if (!epoller_.addfd(wakeup_fd_, kWakeupEvent)) {
        LOG_ERROR("Fail to add wakeup_fd to epoller.");
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
        return false;
    }

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOG_ERROR("Fail to initialize socket.");
        return false;
    }

    int opt = 1;
    if (-1 == ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        LOG_ERROR("Fail to set port reuseable.");
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("Fail to bind.");
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    setNonBlocking(server_fd_);

    if (::listen(server_fd_, SOMAXCONN) < 0) {
        LOG_ERROR("Fail to listen.");
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (!epoller_.addfd(server_fd_, kListenEvent)) {
        LOG_ERROR("Fail to add server_fd to epoller.");
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    LOG_INFO("WebServer listen on port: %d", port_);
    return true;
}

void WebServer::eventLoop() {
    while (is_running_) {
        int timeout = timer_.getNextTick();
        int n = epoller_.wait(timeout);
        timer_.tick();
        if (-1 == n) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll wait error.");
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = epoller_.getEventFd(i);
            uint32_t events = epoller_.getEvents(i);
            if (fd == server_fd_) {
                handleListen();
                continue;
            }
            if (fd == wakeup_fd_) {
                handleWakeup();
                continue;
            }

            if (user_.find(fd) == user_.end()) {
                continue;
            }

            if (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                LOG_INFO("Client(fd = %d) close this connection.", fd);
                closeConn(fd);
            } else {
                LOG_INFO("READ/WRITE event comming.");
                if (events & EPOLLIN) handleRead(fd);
                if (events & EPOLLOUT) handleWrite(fd);
            }
        }
    }
}

void WebServer::handleListen() {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int client_fd = ::accept(server_fd_, (sockaddr*)&addr, &len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            LOG_ERROR("Fail to accept new connection.");
            return;
        }
        setNonBlocking(client_fd);
        auto conn = std::make_shared<HttpConnection>();
        conn->init(client_fd, addr);
        user_[client_fd] = conn;
        if (!epoller_.addfd(client_fd, kReadEvent)) {
            LOG_ERROR("Fail to add client(fd = %d) to epoller_.", client_fd);
            ::close(client_fd);
            user_.erase(client_fd);
            continue;
        }
        timer_.add(client_fd, timeout_ms_, [this, client_fd]() {
            this->closeConn(client_fd);
            LOG_INFO("Client(fd = %d) connection timed out.", client_fd);
        });
        LOG_INFO("New connection client(fd = %d).", client_fd);
    }
}

void WebServer::handleRead(int fd) {
    auto it = user_.find(fd);
    if (it == user_.end()) {
        LOG_ERROR("Fail to find httpconnection for client(fd = %d).", fd);
        return;
    }
    std::shared_ptr<HttpConnection> conn = it->second;
    HttpConnection::HttpReadResult res = conn->read();
    if (res != HttpConnection::HttpReadResult::DONE) {
        if (res == HttpConnection::HttpReadResult::PEER_CLOSE) {
            LOG_INFO("Client(fd = %d) close connection.", fd);
        } else {
            LOG_ERROR("Fail to read from client(fd = %d)", fd);
        }
        closeConn(fd);
        return;
    }

    if (processing_fds_.count(fd) != 0) {
        return;
    }

    processing_fds_.insert(fd);
    if (!epoller_.modfd(fd, kProcessEvent)) {
        LOG_ERROR("Fail to set process events which fd = %d.", fd);
        processing_fds_.erase(fd);
        closeConn(fd);
        return;
    }

    if (!thread_pool_.enqueue([this, fd, conn]() {
            bool ready_to_write = conn->process();
            {
                std::lock_guard<std::mutex> lock(completed_mtx_);
                completed_results_.push({fd, ready_to_write});
            }
            uint64_t one = 1;
            ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
            (void)n;
        })) {
        LOG_ERROR("Fail to submit task for client(fd = %d).", fd);
        processing_fds_.erase(fd);
        closeConn(fd);
        return;
    }
}

void WebServer::handleWrite(int fd) {
    auto it = user_.find(fd);
    if (it == user_.end()) {
        LOG_ERROR("Fail to find httpconnection when handlewrite, which fd = %d", fd);
        return;
    }
    std::shared_ptr<HttpConnection> conn = it->second;
    HttpConnection::HttpWriteResult res = conn->write();
    if (res == HttpConnection::HttpWriteResult::DONE) {
        LOG_INFO("Client(fd = %d) has been writed successfully, switching to read mode.", fd);
        if (!conn->isKeepAlive()) {
            LOG_INFO("Client(fd = %d) response sent, closing connection.", fd);
            closeConn(fd);
            return;
        }
        conn->reset();
        if (!epoller_.modfd(fd, kReadEvent)) {
            LOG_ERROR("Fail to modify events which fd = %d.", fd);
            closeConn(fd);
            return;
        }
    } else if (res == HttpConnection::HttpWriteResult::AGAIN) {
        LOG_INFO("Continue to write data to client(fd = %d).", fd);
    } else {
        LOG_ERROR("Fail to write to client(fd = %d).", fd);
        closeConn(fd);
    }
}

void WebServer::handleWakeup() {
    while (true) {
        uint64_t cnt = 0;
        ssize_t n = ::read(wakeup_fd_, &cnt, sizeof(cnt));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("Fail to read wakeup fd.");
            break;
        }
        if (n == 0) {
            break;
        }
    }

    std::queue<AsyncProcessResult> local_results;
    {
        std::lock_guard<std::mutex> lock(completed_mtx_);
        std::swap(local_results, completed_results_);
    }

    while (!local_results.empty()) {
        AsyncProcessResult result = local_results.front();
        local_results.pop();
        processing_fds_.erase(result.fd);

        auto it = user_.find(result.fd);
        if (it == user_.end()) {
            continue;
        }

        if (result.ready_to_write) {
            if (!epoller_.modfd(result.fd, kWriteEvent)) {
                LOG_ERROR("Fail to modify events to write mode which fd = %d.", result.fd);
                closeConn(result.fd);
                continue;
            }
        } else {
            if (!epoller_.modfd(result.fd, kReadEvent)) {
                LOG_ERROR("Fail to modify events to read mode which fd = %d.", result.fd);
                closeConn(result.fd);
                continue;
            }
        }
        timer_.adjust(result.fd, timeout_ms_);
    }
}

void WebServer::closeConn(int fd) {
    epoller_.delfd(fd);
    processing_fds_.erase(fd);
    auto it = user_.find(fd);
    if (it != user_.end()) {
        it->second->closeConn();
        user_.erase(it);
    } else {
        ::close(fd);
    }
    timer_.remove(fd);
    LOG_INFO("Close the connection, fd = %d.", fd);
}
