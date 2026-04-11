#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "HttpConnection.h"
#include "Logger.h"
#include "LogMacro.h"
#include "Epoller.h"

#define MAX_EVENTS 1024 

int setNonBlock(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

int main() {

    LogConfig config;
    config.log_dir = "logs";
    config.log_base_name = "WebServer";
    config.async_mode = true;
    config.queue_capacity = 2048;
    config.min_level = LogLevel::DEBUG;
    config.max_file_size = 10 * 1024;

    config.flush_every_n_bytes = 64 * 1024;
    config.flush_every_n_message = 64;
    config.flush_internal_ms = 1000;

    LoggerGuard logger_guard(config);

    LOG_INFO("Logger initialize successfully");
    LOG_INFO("WebServer starting...");

    int serv_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serv_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("Fail to bind.");
        return 1;
    }

    setNonBlock(serv_fd);

    if (-1 == listen(serv_fd, 128)) {
        LOG_ERROR("Fail to listen.");
    }

    Epoller epoller(MAX_EVENTS);
    std::unordered_map<int, HttpConnection> users;
    epoller.addfd(serv_fd, EPOLLIN | EPOLLET);

    while (true) {
        int n = epoller.wait(-1);

        for (int i = 0; i < n; ++i) {
            int fd = epoller.getEventFd(i);

            // 新连接
            if (fd == serv_fd) {
                LOG_DEBUG("New connection.");
                while (true) {
                    int client_fd = accept(serv_fd, nullptr, nullptr);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        if (errno == EINTR) continue;
                        if (errno == ECONNABORTED) continue;
                        if (errno == EMFILE || ENFILE) {
                            // fd 耗尽
                            LOG_ERROR("The fd exhausted.");
                            break;
                        }
                        LOG_ERROR("Fail to accept.");
                        break;
                    }
                    epoller.addfd(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT);
                    users.try_emplace(client_fd, client_fd);
                    setNonBlock(client_fd);
                    LOG_INFO("New connection fd = %d", client_fd);
                }
            } else {
                LOG_DEBUG("Other events.");
                auto it = users.find(fd);
                if (it == users.end()) {
                    LOG_ERROR("Cant find HttpConnection for fd = %d.", fd);
                    continue;
                }
                HttpConnection& conn = it->second;
                uint32_t events = epoller.getEvents(i);
                if (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    epoller.delfd(fd);
                    users.erase(fd);
                    LOG_DEBUG("fd = %d is closed.");
                    continue;
                } else if (events & EPOLLIN) {
                    conn.reset();
                    HttpReadResult res = conn.read();
                    if (res == HttpReadResult::ERROR) {
                        LOG_ERROR("Fail to read fd = %d", fd);
                        epoller.delfd(fd);
                        users.erase(fd);
                        continue;
                    } else if (res == HttpReadResult::PEER_CLOSE) {
                        LOG_DEBUG("Peer closed fd = %d.", fd);
                        epoller.delfd(fd);
                        users.erase(fd);
                        continue;
                    }

                    if (conn.process()) {
                        LOG_DEBUG("Process done.\n method = [%s], url = [%s], version = [%s].",
                                    conn.get_method().c_str(), conn.get_url().c_str(), conn.get_version().c_str());
                        epoller.modfd(fd, EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLONESHOT);
                        LOG_DEBUG("Switch to EPOLLOUT.");
                    }
                } else if (events & EPOLLOUT) {
                    HttpWriteResult res = conn.write();
                    if (res == HttpWriteResult::DONE) {
                        epoller.modfd(fd, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT);
                        LOG_DEBUG("Switch to EPOLLIN.");
                    } else if (res == HttpWriteResult::ERROR) {
                        LOG_ERROR("Fail to write fd = %d.", fd);
                        epoller.delfd(fd);
                        users.erase(fd);
                    }
                }
            }
        }
    }
    Logger::getInstance().stop();
    std::cout << "hello world!\n";
    return 0;
}
