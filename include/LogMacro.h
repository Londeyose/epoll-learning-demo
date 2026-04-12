#pragma once

#include "Logger.h"

template <typename T>
constexpr const char* get_file_name(const T* path) {
    const char* filename = path;
    while (*path) {
        if (*path == '/' || *path == '\\') filename = path + 1;
        path++;
    }
    return filename;
}

#define __FILENAME__ get_file_name(__FILE__)

#define LOG_DEBUG(fmt, ...) \
    Logger::getInstance().log(LogLevel::DEBUG, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    Logger::getInstance().log(LogLevel::INFO, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    Logger::getInstance().log(LogLevel::WARN, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    Logger::getInstance().log(LogLevel::ERROR, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
    Logger::getInstance().log(LogLevel::FATAL, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
