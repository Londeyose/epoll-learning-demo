#include "HttpConnection.h"

#include <stdarg.h>
#include <strings.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
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
    int bytes_have_send = 0;
    int byte_to_send = write_idx_;

    while (byte_to_send > 0) {
        int len = ::write(fd_, write_buf_ + bytes_have_send, byte_to_send);

        if (len == -1) {
            // 发送缓冲区满了，等下次 epoll 触发 EPOLLOUT 再发
            // 这不是错误，所以返回 true 保持连接
            if (errno == EAGAIN || errno == EWOULDBLOCK) return HttpWriteResult::AGAIN;
            if (errno == EINTR) continue;
            // 真正错误
            return HttpWriteResult::ERROR;
        }

        bytes_have_send += len;
        byte_to_send -= len;
    }
    write_idx_ = 0;
    return HttpWriteResult::DONE;
}

bool HttpConnection::process() {
    // if (strstr(read_buf_, "\r\n\r\n") == nullptr) {
    //     std::cout << "read_buf_ end error!" << std::endl;
    //     return false;
    // }

    if (parse_request_line(read_buf_) != HttpParseResult::OK) {
        std::cout << "parse eror!" << std::endl;
        return false;
    }

    std::string body = "<html><body><h1>Hello from C++ Server!</h1></body></html>";
    // 开始构建响应
    add_response("%s %d %s\r\n", "HTTP/1.1", 200, "OK");
    add_response("Content-Type: %s\r\n", "text/html");
    add_response("Connection: %s\r\n", "close");
    add_response("Content-Length: %d\r\n", body.size());
    add_response("\r\n");  // 关键的空行
    add_response("%s", body.c_str());

    read_idx_ = 0;

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
    read_idx_ = 0;
    write_idx_ = 0;
    method_.clear();
    url_.clear();
    version_.clear();
}

std::string HttpConnection::get_method() { return method_; }

std::string HttpConnection::get_url() { return url_; }

std::string HttpConnection::get_version() { return version_; }
