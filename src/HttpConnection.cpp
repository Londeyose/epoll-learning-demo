#include "HttpConnection.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

const char* HttpConnection::src_dir_ = "./resources";

HttpConnection::HttpConnection()
    : fd_(-1),
      read_idx_(0),
      write_idx_(0),
      iov_cnt_(0),
      bytes_to_send_(0),
      bytes_have_sent_(0),
      file_addr_(nullptr),
      keep_alive_(false) {
    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&addr_, 0, sizeof(addr_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));
}

HttpConnection::~HttpConnection() {
    closeConn();
}

void HttpConnection::init(int fd, const sockaddr_in& addr) {
    fd_ = fd;
    addr_ = addr;

    read_idx_ = 0;
    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;
    keep_alive_ = false;

    file_addr_ = nullptr;
    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));

    request_.init();
    response_.init();
}

void HttpConnection::closeConn() {
    unmapFile();
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

HttpConnection::HttpReadResult HttpConnection::read() {
    while (true) {
        int remain = READ_BUFFER_SIZE - read_idx_ - 1;
        if (remain <= 0) {
            return HttpReadResult::ERROR;
        }

        ssize_t len = ::read(fd_, read_buf_ + read_idx_, remain);
        if (len > 0) {
            read_idx_ += static_cast<int>(len);
            read_buf_[read_idx_] = '\0';
        } else if (len == 0) {
            return HttpReadResult::PEER_CLOSE;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            return HttpReadResult::ERROR;
        }
    }
    return HttpReadResult::DONE;
}

bool HttpConnection::process() {
    response_.init();
    unmapFile();

    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;

    auto ret = request_.parse(read_buf_, read_idx_);
    if (ret == HttpRequest::ParseResult::INCOMPLELE) {
        return false;
    }

    if (ret == HttpRequest::ParseResult::ERROR) {
        keep_alive_ = false;
        return prepareErrorResponse();
    }

    keep_alive_ = request_.isKeepAlive();

    if (!makeResponse()) {
        return false;
    }

    return prepareWrite();
}

bool HttpConnection::makeResponse() {
    return response_.build(request_, src_dir_, keep_alive_);
}

bool HttpConnection::prepareErrorResponse() {
    static const char* bad_request =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Content-Length: 50\r\n"
        "\r\n"
        "<html><body><h1>400 Bad Request</h1></body></html>";

    int len = static_cast<int>(std::strlen(bad_request));
    if (len >= WRITE_BUFFER_SIZE) {
        return false;
    }

    std::memcpy(write_buf_, bad_request, len);
    write_idx_ = len;

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = write_idx_;
    iov_cnt_ = 1;

    bytes_to_send_ = write_idx_;
    bytes_have_sent_ = 0;
    return true;
}

bool HttpConnection::prepareWrite() {
    const std::string& resp = response_.buffer();
    if (static_cast<int>(resp.size()) >= WRITE_BUFFER_SIZE) {
        return false;
    }

    std::memcpy(write_buf_, resp.data(), resp.size());
    write_idx_ = static_cast<int>(resp.size());

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = write_idx_;
    iov_cnt_ = 1;

    if (response_.statusCode() == 200) {
        if (!mapFile(response_.filePath())) {
            return false;
        }

        iov_[1].iov_base = file_addr_;
        iov_[1].iov_len = file_stat_.st_size;
        iov_cnt_ = 2;
        bytes_to_send_ = write_idx_ + static_cast<int>(file_stat_.st_size);
    } else {
        bytes_to_send_ = write_idx_;
    }

    bytes_have_sent_ = 0;
    return true;
}

HttpConnection::HttpWriteResult HttpConnection::write() {
    if (bytes_to_send_ <= 0) {
        return HttpWriteResult::DONE;
    }

    while (true) {
        ssize_t len = ::writev(fd_, iov_, iov_cnt_);
        if (len < 0) {
            if (errno == EAGAIN) {
                return HttpWriteResult::AGAIN;
            }
            if (errno == EINTR) {
                continue;
            }
            unmapFile();
            return HttpWriteResult::ERROR;
        }

        bytes_have_sent_ += static_cast<int>(len);
        bytes_to_send_ -= static_cast<int>(len);

        int sent = static_cast<int>(len);

        // 只保留你现在已有的“部分写偏移更新”思路
        if (iov_cnt_ >= 1) {
            if (sent >= static_cast<int>(iov_[0].iov_len)) {
                sent -= static_cast<int>(iov_[0].iov_len);
                iov_[0].iov_len = 0;

                if (iov_cnt_ == 2) {
                    iov_[1].iov_base = static_cast<char*>(iov_[1].iov_base) + sent;
                    iov_[1].iov_len -= sent;
                }
            } else {
                iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + sent;
                iov_[0].iov_len -= sent;
            }
        }

        if (bytes_to_send_ <= 0) {
            unmapFile();
            return HttpWriteResult::DONE;
        }
    }
}

bool HttpConnection::mapFile(const std::string& path) {
    int filefd = ::open(path.c_str(), O_RDONLY);
    if (filefd < 0) {
        return false;
    }

    if (::stat(path.c_str(), &file_stat_) < 0) {
        ::close(filefd);
        return false;
    }

    file_addr_ = static_cast<char*>(
        ::mmap(nullptr, file_stat_.st_size, PROT_READ, MAP_PRIVATE, filefd, 0));
    ::close(filefd);

    if (file_addr_ == MAP_FAILED) {
        file_addr_ = nullptr;
        return false;
    }
    return true;
}

void HttpConnection::unmapFile() {
    if (file_addr_) {
        ::munmap(file_addr_, file_stat_.st_size);
        file_addr_ = nullptr;
    }
}

void HttpConnection::reset() {
    unmapFile();

    read_idx_ = 0;
    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;
    keep_alive_ = false;

    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));

    request_.init();
    response_.init();
}