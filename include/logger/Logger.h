#pragma once

#include "BlockingQueue.h"

#include <cstdarg>
#include <atomic>
#include <thread>
#include <fstream>
#include <string>
#include <mutex>
#include <memory>

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
    void write_message(const std::string& message);
    std::string format_message(LogLevel level, const char* file, int line, const char* fmt, va_list args) const;
    const char* level_to_string(LogLevel level) const;
    bool should_log(LogLevel level) const;
    bool open_log_file();
    std::string get_log_file_name() const;

private:
    LogConfig config_;
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;

    std::unique_ptr<BlockingQueue<std::string>> queue_;
    std::thread worker_thread_;

    std::ofstream log_file_;
    std::mutex file_mutex_;
};