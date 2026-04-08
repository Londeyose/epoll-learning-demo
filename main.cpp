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
#define MAX_EVENTS 1024

int setNonBlock(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

int main() {
    int serv_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serv_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("fail to bind!");
        return 1;
    }

    setNonBlock(serv_fd);

    if (-1 == listen(serv_fd, 128)) {
        perror("fail to listen");
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
                while (true) {
                    int client_fd = accept(serv_fd, nullptr, nullptr);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            std::cout << "no more connetion!" << std::endl;
                            break;
                        }
                        if (errno == EINTR) continue;
                        if (errno == ECONNABORTED) continue;
                        if (errno == EMFILE || ENFILE) {
                            // fd 耗尽
                            perror("fd exhausted!");
                            break;
                        }
                        perror("fail to accept!");
                        break;
                    }
                    epoll_event ev{};
                    ev.data.ptr = new HttpConnection(client_fd);
                    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                    setNonBlock(client_fd);
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);

                    std::cout << "new connection fd = " << client_fd << std::endl;
                }
            } else {
                std::cout << "other events" << std::endl;
                HttpConnection* conn = static_cast<HttpConnection*>(events[i].data.ptr);
                int cur_fd = conn->getfd();

                if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                    delete conn;
                    std::cout << "fd = " << cur_fd << " close!" << std::endl;
                    continue;
                } else if (events[i].events & EPOLLIN) {
                    conn->reset();
                    HttpReadResult res = conn->read();
                    if (res == HttpReadResult::ERROR) {
                        std::cout << "fail to read fd = " << cur_fd << std::endl;
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                        continue;
                    } else if (res == HttpReadResult::PEER_CLOSE) {
                        std::cout << "peer closed fd=" << cur_fd << std::endl;  // 正常关闭
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                        continue;
                    }

                    if (conn->process()) {
                        std::cout << "process done! method = [" << conn->get_method() << "] url = ["
                                  << conn->get_url() << "] version = [" << conn->get_version()
                                  << "]" << std::endl;
                        epoll_event ev{};
                        ev.data.ptr = conn;
                        ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, cur_fd, &ev);
                        std::cout << "switch to EPOLLOUT" << std::endl;
                    }
                } else if (events[i].events & EPOLLOUT) {
                    HttpWriteResult res = conn->write();
                    if (res == HttpWriteResult::DONE) {
                        epoll_event ev{};
                        ev.data.ptr = conn;
                        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, cur_fd, &ev);
                        std::cout << "switch to EPOLLIN" << std::endl;
                    } else if (res == HttpWriteResult::ERROR) {
                        std::cout << "fail to wirte fd = " << cur_fd << std::endl;
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, nullptr);
                        delete conn;
                    }
                }
            }
        }
    }

    std::cout << "hello world!\n";
    return 0;
}
