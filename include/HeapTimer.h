#pragma once

#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <algorithm>

using TimeoutCallback = std::function<void()>;
using Clock = std::chrono::high_resolution_clock;
using MS = std::chrono::milliseconds;
using TimeStamp = Clock::time_point;

struct TimerNode {
    int fd;
    TimeStamp expires;
    TimeoutCallback cb;

    bool operator<(TimerNode& t) {
        return expires < t.expires; 
    }
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64);}
    ~HeapTimer() { clear(); }

    // 调整指定id的任务过期时间
    void adjust(int fd, int newTimeout);

    // 添加定时任务
    void add(int fd, int timeout, const TimeoutCallback& cb);

    // 删除指定id的任务并触发回调
    void doWork(int fd);

    // 清理堆
    void clear();

    // 处理过期任务
    void tick();

    // 弹出堆顶
    void pop();

    // 获取下次心跳等待时间
    int getNextTick();

    void remove(int fd);
private:
    void del(size_t index);
    void sift_up(size_t index);
    void sift_down(size_t index, size_t n);
    void swapNode(size_t i, size_t j);

    std::vector<TimerNode> heap_;
    std::unordered_map<int, size_t> ref_;
};