#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "BlockingQueue.h"

class ThreadPool {
public:
    ThreadPool(size_t thread_count = std::thread::hardware_concurrency(), size_t queue_capacity = 1024);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool enqueue(std::function<void()> task);
    void stop();

private:
    void workerLoop();

private:
    BlockingQueue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
};
