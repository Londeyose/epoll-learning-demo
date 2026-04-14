#include "WebServer.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

#include "LogMacro.h"

namespace {
constexpr int MAX_EVENTS = 1024;
constexpr uint32_t kListenEvent = EPOLLIN | EPOLLET;
constexpr uint32_t kReadEvent = EPOLLIN | EPOLLRDHUP | EPOLLET;
constexpr uint32_t kWriteEvent = EPOLLOUT | EPOLLRDHUP | EPOLLET;

int setNonBlocking(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }
}  // namespace

WebServer::WebServer(int port, int trig_mode, int timeout_ms)
    : port_(port),
      timeout_ms_(timeout_ms),
      server_fd_(-1),
      is_running_(false),
      epoller_(MAX_EVENTS) {
    (void)trig_mode;
}

WebServer::~WebServer() {
    if (server_fd_ != -1) {
        ::close(server_fd_);
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
        int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            LOG_ERROR("Fail to accept new connection.");
            return;
        }
        setNonBlocking(client_fd);
        user_.try_emplace(client_fd, client_fd);
        if (!epoller_.addfd(client_fd, kReadEvent)) {
            LOG_ERROR("Fail to add client(fd = %d) to epoller_.", client_fd);
            ::close(client_fd);
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
    HttpConnection& conn = it->second;
    HttpReadResult res = conn.read();
    if (res != HttpReadResult::DONE) {
        if (res == HttpReadResult::PEER_CLOSE) {
            LOG_INFO("Client(fd = %d) close connection.", fd);
        } else {
            LOG_ERROR("Fail to read from client(fd = %d)", fd);
        }
        closeConn(fd);
        return;
    }

    bool ready_to_write = conn.process();
    if (false == ready_to_write) {
        return;
    }

    if (!epoller_.modfd(fd, kWriteEvent)) {
        LOG_ERROR("Fail to modify events which fd = %d.", fd);
        closeConn(fd);
        return;
    }

    timer_.adjust(fd, timeout_ms_);
}

void WebServer::handleWrite(int fd) {
    auto it = user_.find(fd);
    if (it == user_.end()) {
        LOG_ERROR("Fail to find httpconnection when handlewrite, which fd = %d", fd);
        return;
    }
    HttpConnection& conn = it->second;
    HttpWriteResult res = conn.write();
    if (res == HttpWriteResult::DONE) {
        LOG_INFO("Client(fd = %d) has been writed successfully, switching to read mode.", fd);
        if (!conn.is_keep_alive()) {
            LOG_INFO("Client(fd = %d) response sent, closing connection.", fd);
            closeConn(fd);
            return;
        }
        if (!epoller_.modfd(fd, kReadEvent)) {
            LOG_ERROR("Fail to modify events which fd = %d.", fd);
            closeConn(fd);
            return;
        }
    } else if (res == HttpWriteResult::AGAIN) {
        LOG_INFO("Continue to write data to client(fd = %d).", fd);
    } else {
        LOG_ERROR("Fail to write to client(fd = %d).", fd);
        closeConn(fd);
    }
}

void WebServer::closeConn(int fd) {
    epoller_.delfd(fd);
    auto it = user_.find(fd);
    if (it != user_.end()) {
        it->second.close();
        user_.erase(it);
    } else {
        ::close(fd);
    }
    timer_.remove(fd);
    LOG_INFO("Close the connection, fd = %d.", fd);
}