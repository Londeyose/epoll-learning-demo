#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

Logger& Logger::getInstance() {
    static Logger logger;
    return logger;
}

Logger::Logger(): initialized_(false), running_(false) {}

Logger::~Logger() {
    stop();
}

bool Logger::init(const LogConfig& config) {
    if (initialized_) {
        return true;
    }

    config_ = config;

    if (!open_log_file()) {
        return false;
    }

    if (config_.async_mode) {
        queue_ = std::make_unique<BlockingQueue<std::string>>(config_.queue_capacity);
        running_ = true;
        worker_thread_ = std::thread(&Logger::async_wirte_loop, this);
    }
    
    initialized_ = true;
    return true;
}

void Logger::stop() {
    if (!initialized_) {
        return;
    }

    if (config_.async_mode && queue_) {
        running_ = false;
        queue_->close();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
            log_file_.close();
        } 
    }

    initialized_ = false;
}

bool Logger::should_log(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(config_.min_level);
}

void Logger::log(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (!initialized_ || !should_log(level)) {
        return ;
    }

    va_list args;
    va_start(args, fmt);
    std::string message = format_message(level, file, line, fmt, args);
    va_end(args);

    if (config_.async_mode && queue_) {
        if (!queue_->push(std::move(message))) {

        }
    } else {
        write_message(message);
    }
}

void Logger::async_wirte_loop() {
    std::string message;
    while (queue_ && queue_->pop(message)) {
        write_message(message);
    }
}

void Logger::write_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (log_file_.is_open()) {
        log_file_ << message;
        log_file_.flush();
    }
}

const char* Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARN:    return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::FATAL:    return "FATAL";
        default:                return "UNKNOWN";
    }
}

std::string Logger::format_message(LogLevel level, const char* file, int line, const char* fmt, va_list args) const {
    char message_buffer[4096];
    vsnprintf(message_buffer, sizeof(message_buffer), fmt, args);

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::tm tm_time{};
    localtime_r(&time_t_now, &tm_time);

    std::ostringstream oss;
    oss << '[' << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms.count() << ']'
        << " [" << level_to_string(level) << ']'
        << " [tid:" << std::this_thread::get_id() << ']'
        << " [" << file << ':' << line << "] "
        << message_buffer << '\n';

    return oss.str();
}

bool Logger::open_log_file() {
    try {
        if (!fs::exists(config_.log_dir)) {
            fs::create_directories(config_.log_dir);
        }
    } catch (const std::exception&) {
        return false;
    }

    std::string file_name = get_log_file_name();

    std::lock_guard<std::mutex> lock(file_mutex_);
    log_file_.open(file_name, std::ios::out | std::ios::app);
    return log_file_.is_open();
}

std::string Logger::get_log_file_name() const {
    return config_.log_dir + "/" + config_.log_base_name + ".log";
}