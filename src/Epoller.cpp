#include "Epoller.h"
#include "Logger.h"
#include "LogMacro.h"
#include <cerrno>
#include <cstring>
#include <unistd.h>

Epoller::Epoller(int max_events) :epoll_fd_(epoll_create1(0)), events_(max_events) {
    if (epoll_fd_ < 0) {
        LOG_ERROR("epoll_create1 failed.");
    }
}

Epoller::~Epoller() {
    if (epoll_fd_ > 0) {
        ::close(epoll_fd_);
    }
}

bool Epoller::addfd(int fd, uint32_t events) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = events;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Epoller::modfd(int fd, uint32_t events) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = events;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Epoller::delfd(int fd) {
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0; 
}


int Epoller::wait(int timeout_ms) {
    return ::epoll_wait(epoll_fd_, events_.data(), events_.size(), timeout_ms);
}


int Epoller::getEventFd(size_t i) const {
    return events_[i].data.fd;
}


uint32_t Epoller::getEvents(size_t i) const {
    return events_[i].events;
}