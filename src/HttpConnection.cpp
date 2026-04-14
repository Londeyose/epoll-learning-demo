#include "HttpConnection.h"

#include <fcntl.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "LogMacro.h"

HttpConnection::HttpConnection(int fd)
    : fd_(fd),
      read_idx_(0),
      write_idx_(0),
      file_addr_(nullptr),
      file_size_(0),
      head_sent_(0),
      file_sent_(0) {
    read_buf_[0] = '\0';
    write_buf_[0] = '\0';
    checked_idx_ = 0;
    start_line_ = 0;
    parse_state_ = ParseState::REQUEST_LINE;
    content_length_ = 0;
    keep_alive_ = false;
}

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

void HttpConnection::adjust_iov(struct iovec* iov, int iovcnt, int sent) {
    for (int i = 0; i < iovcnt; ++i) {
        if (sent >= (int)iov[i].iov_len) {
            sent -= iov[i].iov_len;
            iov[i].iov_len = 0;
        } else {
            iov[i].iov_base = (char*)iov[i].iov_base + sent;
            iov[i].iov_len -= sent;
            break;
        }
    }
}

HttpWriteResult HttpConnection::write() {
    struct iovec iov[2];
    iov[0].iov_base = write_buf_;
    iov[0].iov_len = write_idx_;
    iov[1].iov_base = file_addr_;
    iov[1].iov_len = file_size_;

    int iovcnt = file_size_ == 0 ? 1 : 2;
    int total_size = write_idx_ + file_size_;
    int total_sent = 0;

    while (total_sent < total_size) {
        int len = ::writev(fd_, iov, iovcnt);

        if (len > 0) {
            adjust_iov(iov, iovcnt, len);
            total_sent += len;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return HttpWriteResult::AGAIN;
            if (errno == EINTR) continue;
            return HttpWriteResult::ERROR;
        }
    }

    reset_response_state();
    return HttpWriteResult::DONE;
}

bool HttpConnection::is_keep_alive() const { return keep_alive_; }

bool HttpConnection::process() {
    HttpParseResult ret = process_read();
    LOG_DEBUG("process_read ret = %d", static_cast<int>(ret));
    if (ret == HttpParseResult::INCOMPLETE) {
        return false;
    }

    if (ret != HttpParseResult::OK) {
        LOG_ERROR("Parse error! read_buf = {%s}", read_buf_);
        build_error_response(400, "Bad Request");
        return true;
    }

    return process_request();
}

void HttpConnection::prepare_for_next_request() {
    compact_read_buffer();
    reset_request_state();
}

bool HttpConnection::process_request() {
    read_idx_ = 0;

    std::string file_path = "resources" + url_;

    LOG_DEBUG("Client[fd=%d] request file [%s].", fd_, file_path.c_str());
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) == -1) {
        LOG_DEBUG("file error! response code = %d.", 404);
        build_error_response(404, "Not Found");
        return true;
    }

    if (!S_ISREG(file_stat.st_mode) || !(file_stat.st_mode & S_IROTH)) {
        LOG_DEBUG("file error! response code = %d.", 403);
        build_error_response(403, "Forbidden");
        return true;
    }

    int file_fd = open(file_path.c_str(), O_RDONLY);
    file_addr_ = mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);

    ::close(file_fd);

    if (file_addr_ == MAP_FAILED) {
        LOG_DEBUG("file error! response code = %d.", 500);
        build_error_response(500, "Internal Server Error");
        return true;
    }
    file_size_ = file_stat.st_size;
    LOG_DEBUG("Successful to mapping file! file_size = %d.", file_size_);
    build_ok_response(200, file_stat.st_size);
    return true;
}

void HttpConnection::close() { ::close(fd_); }

HttpParseResult HttpConnection::parse_request_line(char* text) {
    LOG_DEBUG("parse_request_line input = {%s}", text);
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
        LOG_ERROR("Not support Method POST.");
        return HttpParseResult::INVALID_METHOD;
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

    if (strcasecmp(version, "HTTP/1.1") == 0) {
        version_ = "HTTP/1.1";
        keep_alive_ = true;  // HTTP/1.1 默认长连接
    } else if (strcasecmp(version, "HTTP/1.0") == 0) {
        version_ = "HTTP/1.0";
        keep_alive_ = false;  // HTTP/1.0 默认短连接
    } else {
        return HttpParseResult::INVALID_VERSION;
    }
    LOG_DEBUG("method = {%s}", method);
    LOG_DEBUG("url = {%s}", url);
    LOG_DEBUG("version = {%s}", version);
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
    add_response("Connection: %s\r\n", keep_alive_ ? "keep-alive" : "close");
    add_response("Content-Length: %zu\r\n", file_size);
    add_response("\r\n");  // 关键的空行
}

void HttpConnection::build_error_response(int code, const char* reason) {
    write_idx_ = 0;
    head_sent_ = 0;
    add_response("%s %d %s\r\n", "HTTP/1.1", code, reason);
    add_response("Content-Type: %s\r\n", get_content_type(url_));
    add_response("Connection: %s\r\n", "close");
    add_response("Content-Length: %zu\r\n", 0);
    add_response("\r\n");
}

LineStatus HttpConnection::parse_line() {
    for (; checked_idx_ < read_idx_; ++checked_idx_) {
        char ch = read_buf_[checked_idx_];

        if (ch == '\r') {
            if (checked_idx_ + 1 == read_idx_) {
                return LineStatus::LINE_OPEN;
            }

            if (read_buf_[checked_idx_ + 1] == '\n') {
                read_buf_[checked_idx_] = 0;
                read_buf_[checked_idx_ + 1] = 0;
                checked_idx_ += 2;
                return LineStatus::LINE_OK;
            }
        }

        if (ch == '\n') {
            if (checked_idx_ > 0 && read_buf_[checked_idx_ - 1] == '\r') {
                read_buf_[checked_idx_ - 1] = 0;
                read_buf_[checked_idx_] = 0;
                ++checked_idx_;
                return LineStatus::LINE_OK;
            }
            return LineStatus::LINE_BAD;
        }
    }
    return LineStatus::LINE_OPEN;
}

HttpParseResult HttpConnection::parse_headers(char* text) {
    // 空行：说明请求头结束
    if (text[0] == '\0') {
        if (content_length_ != 0) {
            parse_state_ = ParseState::BODY;
            return HttpParseResult::INCOMPLETE;
        }
        parse_state_ = ParseState::FINISH;
        return HttpParseResult::OK;
    }

    if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            keep_alive_ = true;
        } else if (strcasecmp(text, "close") == 0) {
            keep_alive_ = false;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        content_length_ = static_cast<size_t>(atoi(text));
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    } else {
        // 其他 header 先忽略
    }

    return HttpParseResult::INCOMPLETE;
}

HttpParseResult HttpConnection::process_read() {
    LineStatus line_status = LineStatus::LINE_OK;
    char* text = nullptr;
    while (true) {
        line_status = parse_line();
        if (line_status == LineStatus::LINE_OPEN) {
            break;  // 当前还没有完整一行，等下次 read
        }

        if (line_status == LineStatus::LINE_BAD) {
            return HttpParseResult::INVALID_REQUEST;
        }

        text = read_buf_ + start_line_;
        start_line_ = checked_idx_;

        switch (parse_state_) {
            case ParseState::REQUEST_LINE: {
                HttpParseResult ret = parse_request_line(text);
                if (ret != HttpParseResult::OK) {
                    return ret;
                }
                parse_state_ = ParseState::HEADERS;
                break;
            }

            case ParseState::HEADERS: {
                HttpParseResult ret = parse_headers(text);
                if (ret == HttpParseResult::OK) {
                    return HttpParseResult::OK;  // 整个请求头解析完成
                }
                if (ret != HttpParseResult::INCOMPLETE) {
                    return ret;
                }
                break;
            }

            case ParseState::BODY: {
                // 你当前先不处理 POST body
                // 这里先留接口，后面再加
                return HttpParseResult::OK;
            }

            default:
                return HttpParseResult::INVALID_REQUEST;
        }
    }

    return HttpParseResult::INCOMPLETE;
}

void HttpConnection::reset_request_state() {
    method_.clear();
    url_.clear();
    version_.clear();
    host_.clear();

    content_length_ = 0;

    parse_state_ = ParseState::REQUEST_LINE;
    keep_alive_ = false;

    start_line_ = 0;
    checked_idx_ = 0;
}

void HttpConnection::reset_response_state() {
    if (file_addr_) {
        munmap(file_addr_, file_size_);
        file_addr_ = nullptr;
    }

    file_size_ = 0;
    write_idx_ = 0;

    head_sent_ = 0;
    file_sent_ = 0;

    write_buf_[0] = '\0';
}

void HttpConnection::compact_read_buffer() {
    if (start_line_ == 0) {
        return;
    }

    size_t remain = read_idx_ - start_line_;
    if (remain > 0) {
        memmove(read_buf_, read_buf_ + start_line_, remain);
    }

    read_idx_ = remain;
    checked_idx_ = 0;
    start_line_ = 0;

    if (read_idx_ < READ_BUFFER_SIZE) {
        read_buf_[read_idx_] = '\0';
    }
}