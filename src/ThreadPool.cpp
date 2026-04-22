#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t thread_count, size_t queue_capacity)
    : tasks_(queue_capacity) {
    if (thread_count == 0) {
        thread_count = 4;
    }

    workers_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

bool ThreadPool::enqueue(std::function<void()> task) {
    return tasks_.push(std::move(task));
}

void ThreadPool::stop() {
    tasks_.close();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::workerLoop() {
    std::function<void()> task;
    while (tasks_.pop(task)) {
        if (task) {
            task();
        }
    }
}
