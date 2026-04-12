#pragma once

#include <vector>
#include <sys/epoll.h>
class Epoller {
public:
    Epoller(int max_events = 1024);
    ~Epoller();

    bool addfd(int fd, uint32_t events);
    bool modfd(int fd, uint32_t events);
    bool delfd(int fd);

    int wait(int timeout_ms = -1); 
    
    int getEventFd(size_t i) const;
    uint32_t getEvents(size_t i) const;

private:
    int epoll_fd_;
    std::vector<epoll_event> events_;
};