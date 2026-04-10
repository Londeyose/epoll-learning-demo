#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t capacity)
        :capacity_(capacity), closed_(false) {}

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    bool push(T&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!closed_ && queue_.size() > capacity_) {
            not_full_cv_.wait(lock);
        }
        
        if (closed_) {
            return false;
        }

        queue_.push(std::move(value));
        not_empty_cv_.notify_one();
        return true;
    }

    bool push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!closed_ && queue_.size() > capacity_) {
            not_full_cv_.wait(lock);
        }

        if (closed_) {
            return false;
        }

        queue_.push(std::move(value));
        not_empty_cv_.notify_one();
        return true;
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!closed_ && queue_.empty()) {
            not_empty_cv_.wait(lock);
        }

        if (closed_) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    void empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }
private:
    std::queue<T>           queue_;
    size_t                  capacity_;
    bool                    closed_;
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
};