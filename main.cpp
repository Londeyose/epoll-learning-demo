#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "HttpConnection.h"
#include "Logger.h"
#include "LogMacro.h"
#define MAX_EVENTS 1024

int setNonBlock(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

int main() {

    LogConfig config;
    config.log_dir = "logs";
    config.log_base_name = "WebServer";
    config.async_mode = true;
    config.queue_capacity = 2048;
    config.min_level = LogLevel::DEBUG;

    if (!Logger::getInstance().init(config)) {
        perror("Fail to initialize Logger.");
        return -1;
    }

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
    int epfd = epoll_create1(0);

    epoll_event ev{}, events[MAX_EVENTS];

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = serv_fd;

    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_fd, &ev);

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

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
                    epoll_event ev{};
                    ev.data.ptr = new HttpConnection(client_fd);
                    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                    setNonBlock(client_fd);
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                    LOG_INFO("New connection fd = %d", client_fd);
                }
            } else {
                LOG_DEBUG("Other events.");
                HttpConnection* conn = static_cast<HttpConnection*>(events[i].data.ptr);
                int cur_fd = conn->getfd();

                if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                    delete conn;
                    LOG_DEBUG("fd = %d is closed.");
                    continue;
                } else if (events[i].events & EPOLLIN) {
                    conn->reset();
                    HttpReadResult res = conn->read();
                    if (res == HttpReadResult::ERROR) {
                        LOG_ERROR("Fail to read fd = %d", cur_fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                        continue;
                    } else if (res == HttpReadResult::PEER_CLOSE) {
                        LOG_DEBUG("Peer closed fd = %d.", cur_fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                        continue;
                    }

                    if (conn->process()) {
                        LOG_DEBUG("Process done.\n method = [%s], url = [%s], version = [%s].",
                                    conn->get_method(), conn->get_url(), conn->get_version());
                        epoll_event ev{};
                        ev.data.ptr = conn;
                        ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, cur_fd, &ev);
                        LOG_DEBUG("Switch to EPOLLOUT.");
                    }
                } else if (events[i].events & EPOLLOUT) {
                    HttpWriteResult res = conn->write();
                    if (res == HttpWriteResult::DONE) {
                        epoll_event ev{};
                        ev.data.ptr = conn;
                        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, cur_fd, &ev);
                        LOG_DEBUG("Switch to EPOLLIN.");
                    } else if (res == HttpWriteResult::ERROR) {
                        LOG_ERROR("Fail to write fd = %d.", cur_fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                    }
                }
            }
        }
    }
    Logger::getInstance().stop();
    std::cout << "hello world!\n";
    return 0;
}
