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
    current_date_ = current_date_string();
    current_file_index_ = 0;
    current_file_size_ = 0;

    if (!open_log_file()) {
        perror("Fail to open log file!");
        return false;
    }

    if (config_.async_mode) {
        queue_ = std::make_unique<BlockingQueue<LogItem>>(config_.queue_capacity);
        running_ = true;
        worker_thread_ = std::thread(&Logger::async_wirte_loop, this);
    }

    last_flush_time_ = std::chrono::steady_clock::now();
    pending_message_count_ = 0;
    pending_bytes_count_ = 0;
    
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
        LogItem logItem{level, message};
        if (!queue_->push(std::move(logItem))) {
            write_message(level, message);
        }
    } else {
        write_message(level, message);
    }
}

void Logger::async_wirte_loop() {
    LogItem logItem;
    while (queue_ && queue_->pop(logItem)) {
        write_message(logItem.level, logItem.message);
    }

    std::lock_guard<std::mutex> lock(file_mutex_);
    flush_unlocked();
}

void Logger::write_message(LogLevel level, std::string& message) {
    std::lock_guard<std::mutex> lock(file_mutex_);

    if (!rotate_if_needed(message.size())) {
        return ;
    }

    if (!log_file_.is_open()) {
        return ;
    }

    log_file_ << message;
    current_file_size_ += message.size();

    pending_message_count_ += 1;
    pending_bytes_count_ += message.size();

    if (should_flush(level)) {
        flush_unlocked();
    }
}

bool Logger::should_flush(LogLevel level) const {
    if (LogLevel::ERROR == level || LogLevel::FATAL == level) {
        return true;
    }

    if (pending_bytes_count_ >= config_.flush_every_n_bytes) {
        return true;
    }

    if (pending_message_count_ >= config_.flush_every_n_message) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = 
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time_).count();
    
    if (static_cast<size_t>(elapsed_ms) >= config_.flush_internal_ms) {
        return true;
    }
    return false;
}

void Logger::flush_unlocked() {
    if (!log_file_.is_open()) {
        return;
    }
    log_file_.flush();
    pending_bytes_count_ = 0;
    pending_message_count_ = 0;
    last_flush_time_ = std::chrono::steady_clock::now();
}

bool Logger::rotate_if_needed(size_t incomming_message_size) {
    std::string today = current_date_string();

    bool need_rotate_by_day = (today != current_date_);
    bool need_rotate_by_size = current_file_size_ + incomming_message_size > config_.max_file_size;

    if (!need_rotate_by_day && !need_rotate_by_size) {
        return true;
    }

    if (log_file_.is_open()) {
        flush_unlocked();
    }

    if (need_rotate_by_day) {
        current_date_ = today;
        current_file_index_ = 0;
    } else if (need_rotate_by_size) {
        current_file_index_ += 1;
    }
    close_log_file();
    current_file_size_ = 0;
    return open_log_file();
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
    std::string file_name = build_log_file_name(current_date_, current_file_index_);
    std::cout << "log_file_name : " << file_name << std::endl;
    // 1. 先检查并创建文件夹
    try {
        if (!fs::exists(config_.log_dir)) {
            // create_directories 会递归创建所有不存在的父目录
            fs::create_directories(config_.log_dir); 
            std::cout << "Create the log dir." << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Fail to create the log dir." << e.what() << std::endl;
        return 1;
    }
    log_file_.open(file_name, std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        return false;
    }

    try {
        if (fs::exists(file_name)) {
            current_file_size_ = fs::file_size(file_name); 
        } else {
            current_file_size_ = 0;
        }
    } catch (const std::exception&) {
        current_file_size_ = 0;
    }

    return true;
}

void Logger::close_log_file() {
    if (log_file_.is_open()) {
        log_file_.flush();
        log_file_.close();
    }
}

std::string Logger::current_date_string() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_time{};

    localtime_r(&time_t_now, &tm_time);

    std::ostringstream oss;
    oss << std::put_time(&tm_time, "%Y%m%d");
    return oss.str();
}

std::string Logger::build_log_file_name(const std::string& date_str, size_t index) const {
    std::ostringstream oss;
    oss << config_.log_dir << '/'
        << config_.log_base_name << '_'
        << date_str << '_'
        << index << ".log";
    return oss.str();
}

std::string Logger::get_log_file_name() const {
    return config_.log_dir + "/" + config_.log_base_name + ".log";
}