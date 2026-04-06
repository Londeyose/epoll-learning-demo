#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iostream>

#define MAX_EVENTS 1024

int setNonBlock(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

int main() {
    int serv_fd = socket(AF_INET, SOCK_STREAM, 0);

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

    ev.events = EPOLLIN;
    ev.data.fd = serv_fd;

    epoll_ctl(epfd, EPOLL_CTL_ADD, serv_fd, &ev);

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == serv_fd) {
                int client_fd = accept(serv_fd, nullptr, nullptr);

                std::cout << "new connection! fd = " << client_fd << std::endl;
                setNonBlock(client_fd);
                epoll_event ev{};
                ev.data.fd = client_fd;
                ev.events = EPOLLIN;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
            } else {
                char buf[124];
                int len = read(fd, buf, sizeof(buf) - 1);
                if (len > 0) {
                    buf[len] = 0;
                    std::cout << "from fd = " << fd << " : " << buf << std::endl;
                } else if (len == 0) {
                    std::cout << "client fd = " << fd << " close connection" << std::endl;
                    close(fd);
                    break;
                }
            }
        }
    }

    std::cout << "hello world!\n";
    return 0;
}
