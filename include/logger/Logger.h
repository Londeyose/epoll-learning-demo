#pragma once

#include "BlockingQueue.h"

#include <cstdarg>
#include <atomic>
#include <thread>
#include <fstream>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>

enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR,
    FATAL
};

struct LogConfig {
    std::string log_dir = "logs";
    std::string log_base_name = "server";
    size_t queue_capacity = 1024;
    LogLevel min_level = LogLevel::DEBUG;
    bool async_mode = true;
    size_t max_file_size = 10 * 1024 * 1024;

    size_t flush_every_n_message = 64;
    size_t flush_every_n_bytes = 64 * 1024;
    size_t flush_internal_ms = 1000;
};

struct LogItem {
    LogLevel level;
    std::string message;
};

class Logger {
public:
    static Logger& getInstance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool init(const LogConfig& config);
    void stop();

    void log(LogLevel level, const char* file, int line, const char* fmt, ...);

private:
    Logger();
    ~Logger();

    void async_wirte_loop();
    void write_message(LogLevel level, std::string& message);

    std::string format_message(LogLevel level, const char* file, int line, 
                                const char* fmt, va_list args) const;

    const char* level_to_string(LogLevel level) const;
    bool should_log(LogLevel level) const;

    bool open_log_file();
    bool rotate_if_needed(size_t incoming_message_size);
    void close_log_file();

    std::string current_date_string() const;
    std::string build_log_file_name(const std::string& date, size_t index) const;
    std::string get_log_file_name() const;

    bool should_flush(LogLevel level) const;
    void flush_unlocked();

private:
    LogConfig config_;
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;

    std::unique_ptr<BlockingQueue<LogItem>> queue_;
    std::thread worker_thread_;

    std::ofstream log_file_;
    std::mutex file_mutex_;

    std::string current_date_;
    size_t current_file_index_{0};
    size_t current_file_size_{0};

    size_t pending_message_count_{0};
    size_t pending_bytes_count_{0};
    std::chrono::steady_clock::time_point last_flush_time_;
};

class LoggerGuard {
public:
    explicit LoggerGuard(const LogConfig& config) {
        Logger::getInstance().init(config);
    }
    ~LoggerGuard() {
        Logger::getInstance().stop();
    }

    LoggerGuard(const LoggerGuard&) = delete;
    LoggerGuard& operator=(const LoggerGuard&) = delete;
};