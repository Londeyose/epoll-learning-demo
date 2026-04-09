#include "HttpConnection.h"

#include <fcntl.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
HttpConnection::HttpConnection(int fd) { fd_ = fd; }

HttpConnection::~HttpConnection() { ::close(fd_); }

HttpReadResult HttpConnection::read() {
    while (true) {
        int remaining = READ_BUFFER_SIZE - read_idx_ - 1;
        if (remaining <= 0) return HttpReadResult::ERROR;  // 缓冲区满了
        int len = ::read(fd_, read_buf_ + read_idx_, remaining);

        if (len > 0) {
            read_idx_ += len;
            read_buf_[read_idx_] = 0;
        } else if (len == 0) {
            return HttpReadResult::PEER_CLOSE;
        } else {
            // 读完了， epoll 触发 EPOLLIN 再读
            // 这不是错误，所以返回 true 保持连接
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            // 真正错误
            return HttpReadResult::ERROR;
        }
    }
    return HttpReadResult::DONE;
}

HttpWriteResult HttpConnection::write() {
    // 第一阶段 发送响应头

    while (head_sent_ < write_idx_) {
        int len = ::write(fd_, write_buf_ + head_sent_, write_idx_ - head_sent_);

        if (len == -1) {
            // 发送缓冲区满了，等下次 epoll 触发 EPOLLOUT 再发
            // 这不是错误，所以返回 true 保持连接
            if (errno == EAGAIN || errno == EWOULDBLOCK) return HttpWriteResult::AGAIN;
            if (errno == EINTR) continue;
            // 真正错误
            return HttpWriteResult::ERROR;
        }

        head_sent_ += len;
    }

    write_idx_ = 0;
    std::cout << "write called: head_sent_=" << head_sent_ << " write_idx_=" << write_idx_
              << " file_sent_=" << file_sent_ << " file_size_=" << file_size_
              << " file_addr_=" << file_addr_ << std::endl;
    while (file_sent_ < file_size_) {
        int len = ::write(fd_, (char*)file_addr_ + file_sent_, file_size_ - file_sent_);
        if (len == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return HttpWriteResult::AGAIN;
            if (errno == EINTR) continue;
            return HttpWriteResult::ERROR;
        }
        file_sent_ += len;
    }
    reset();
    return HttpWriteResult::DONE;
}

bool HttpConnection::process() {
    if (parse_request_line(read_buf_) != HttpParseResult::OK) {
        std::cout << "parse eror!" << std::endl;
        return false;
    }

    read_idx_ = 0;

    std::string file_path = "resources" + url_;

    std::cout << "file:" << file_path << std::endl;

    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) == -1) {
        std::cout << 404 << std::endl;
        build_error_response(404, "Not Found");
        return true;
    }

    if (!S_ISREG(file_stat.st_mode) || !(file_stat.st_mode & S_IROTH)) {
        std::cout << 403 << std::endl;
        build_error_response(403, "Forbidden");
        return true;
    }

    int file_fd = open(file_path.c_str(), O_RDONLY);
    file_addr_ = mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);

    ::close(file_fd);

    if (file_addr_ == MAP_FAILED) {
        std::cout << 500 << std::endl;
        build_error_response(500, "Internal Server Error");
        return true;
    }
    file_size_ = file_stat.st_size;
    std::cout << 200 << std::endl;
    build_ok_response(200, file_stat.st_size);

    return true;
}

void HttpConnection::close() { ::close(fd_); }

HttpParseResult HttpConnection::parse_request_line(char* text) {
    // 方法
    text += strspn(text, " \t");
    char* url = strpbrk(text, " \t");
    if (!url) return HttpParseResult::INVALID_REQUEST;
    *url++ = '\0';
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        method_ = "GET";
    } else if (strcasecmp(method, "POST") == 0) {
        method_ = "POST";
    } else {
        return HttpParseResult::INVALID_METHOD;
    }

    // 提取url
    char* version = strpbrk(url, " \t");
    if (!version) return HttpParseResult::INVALID_REQUEST;
    *version++ = '\0';

    // 去掉url尾部空格
    char* end = url + strlen(url) - 1;
    while (end > url && (*end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }

    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/');
    }

    if (!url || url[0] == '\0') {
        url_ = "/index.html";
    } else {
        url_ = url;
        if (url_ == "/") url_ = "/index.html";
    }
    char* end_of_line = strpbrk(version, "\r\n");
    if (end_of_line) *end_of_line = '\0';
    // 处理http版本
    if (strcasecmp(version, "HTTP/1.1") != 0 && strcasecmp(version, "HTTP/1.0") != 0) {
        return HttpParseResult::INVALID_VERSION;
    }
    version_ = version;
    return HttpParseResult::OK;
}

bool HttpConnection::add_response(const char* format, ...) {
    if (write_idx_ >= WRITE_BUFFER_SIZE) return false;

    va_list arg_list;
    va_start(arg_list, format);

    int len =
        vsnprintf(write_buf_ + write_idx_, WRITE_BUFFER_SIZE - write_idx_ - 1, format, arg_list);

    if (len >= (WRITE_BUFFER_SIZE - write_idx_ - 1)) {
        va_end(arg_list);
        return false;
    }

    write_idx_ += len;
    va_end(arg_list);
    return true;
}

int HttpConnection::getfd() { return fd_; }

void HttpConnection::reset() {
    if (file_addr_) {
        munmap(file_addr_, file_size_);
        file_addr_ = nullptr;
    }
    read_idx_ = 0;
    write_idx_ = 0;
    head_sent_ = 0;
    file_sent_ = 0;
    method_.clear();
    url_.clear();
    version_.clear();
}

std::string HttpConnection::get_method() { return method_; }

std::string HttpConnection::get_url() { return url_; }

std::string HttpConnection::get_version() { return version_; }

const char* HttpConnection::get_content_type(const std::string& url) {
    auto ends_with = [](const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size()) return false;
        return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(url, ".html")) return "text/html";
    if (ends_with(url, ".css")) return "text/css";
    if (ends_with(url, ".js")) return "application/javascript";
    if (ends_with(url, ".png")) return "image/png";
    if (ends_with(url, ".jpg")) return "image/jpeg";
    if (ends_with(url, ".ico")) return "image/x-icon";

    return "text/plain";
}

void HttpConnection::build_ok_response(int code, size_t file_size) {
    write_idx_ = 0;
    head_sent_ = 0;
    // 开始构建响应
    add_response("%s %d %s\r\n", "HTTP/1.1", code, "OK");
    add_response("Content-Type: %s\r\n", get_content_type(url_));
    add_response("Connection: %s\r\n", "close");
    add_response("Content-Length: %d\r\n", file_size);
    add_response("\r\n");  // 关键的空行
}

void HttpConnection::build_error_response(int code, const char* reason) {
    write_idx_ = 0;
    head_sent_ = 0;
    add_response("%s %d %s\r\n", "HTTP/1.1", code, reason);
    add_response("Content-Type: %s\r\n", get_content_type(url_));
    add_response("Connection: %s\r\n", "close");
    add_response("Content-Length: %d\r\n", 0);
    add_response("\r\n");
}
