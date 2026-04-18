#include "HeapTimer.h"

#include <iostream>

#include "LogMacro.h"
void HeapTimer::sift_up(size_t index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap_[index] < heap_[parent]) {
            swapNode(parent, index);
            index = parent;
        } else {
            break;
        }
    }
}

void HeapTimer::swapNode(size_t i, size_t j) {
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].fd] = i;
    ref_[heap_[j].fd] = j;
}

void HeapTimer::sift_down(size_t index, size_t n) {
    while (true) {
        size_t c1 = index * 2 + 1;
        size_t c2 = index * 2 + 2;
        size_t min = index;
        if (c1 < n && heap_[c1] < heap_[min]) min = c1;
        if (c2 < n && heap_[c2] < heap_[min]) min = c2;
        if (min != index) {
            swapNode(min, index);
            index = min;
        } else {
            break;
        }
    }
}

void HeapTimer::add(int fd, int timeout, const TimeoutCallback& cb) {
    if (ref_.find(fd) != ref_.end()) {
        LOG_ERROR("client(fd=%d) has been added into heap_.", fd);
        return;
    }
    size_t index = heap_.size();
    ref_[fd] = index;
    heap_.push_back({fd, Clock::now() + MS(timeout), cb});
    sift_up(index);
}

void HeapTimer::doWork(int fd) {
    if (heap_.empty() || ref_.find(fd) == ref_.end()) {
        return;
    }
    size_t i = ref_[fd];
    auto cb = heap_[i].cb;
    if (cb) {
        cb();
    }

    // 回调里可能已经通过 closeConn()->timer_.remove(fd) 删除了该节点，
    // 这里需要再次确认，避免重复删除导致误删其它 fd 的定时器。
    auto it = ref_.find(fd);
    if (it != ref_.end()) {
        del(it->second);
    }
}

void HeapTimer::remove(int fd) {
    if (heap_.empty() || ref_.find(fd) == ref_.end()) {
        return;
    }
    size_t i = ref_[fd];
    del(i);
}

void HeapTimer::del(size_t index) {
    size_t n = heap_.size() - 1;
    if (index < n) {
        swapNode(index, n);
        sift_down(index, n);
        sift_up(index);
    }
    ref_.erase(heap_.back().fd);
    heap_.pop_back();
}

void HeapTimer::adjust(int fd, int timeout) {
    if (ref_.find(fd) == ref_.end()) return;
    size_t i = ref_[fd];
    heap_[i].expires = Clock::now() + MS(timeout);
    sift_down(i, heap_.size());
    sift_up(i);
}

void HeapTimer::tick() {
    if (heap_.empty()) {
        return;
    }
    while (!heap_.empty()) {
        TimerNode node = heap_.front();
        if (node.expires > Clock::now()) {
            break;
        }
        doWork(node.fd);
    }
}

void HeapTimer::pop() {
    if (heap_.empty()) return;
    del(0);
}

void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

int HeapTimer::getNextTick() {
    tick();
    if (heap_.empty()) return -1;

    int res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();

    return res > 0 ? res : 0;
}
