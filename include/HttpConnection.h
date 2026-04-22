#pragma once

#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "SqlConnRAII.h"

class HttpConnection {
public:
    static constexpr int READ_BUFFER_SIZE = 4096;
    static constexpr int WRITE_BUFFER_SIZE = 4096;
    static const char* src_dir_;

public:
    enum class HttpReadResult {
        DONE,
        PEER_CLOSE,
        ERROR
    };

    enum class HttpWriteResult {
        DONE,
        AGAIN,
        ERROR
    };

public:
    HttpConnection();
    ~HttpConnection();

    void init(int fd, const sockaddr_in& addr);
    void closeConn();

    HttpReadResult read();
    HttpWriteResult write();

    bool process();     // 解析请求并准备响应
    void reset();       // keep-alive 复用时重置状态

    bool isKeepAlive() const { return keep_alive_; }
    bool needClose() const { return !keep_alive_; }
    int fd() const { return fd_; }

private:
    bool makeResponse();
    bool prepareWrite();
    bool prepareErrorResponse();

    bool mapFile(const std::string& path);
    void unmapFile();

    bool handleRegister();
    bool handleLogin();
    bool handleWhoAmI();
    static std::string generateSessionId(const std::string& username);
    static std::string getCookieValue(const std::string& cookie_header, const std::string& key);
private:
    int fd_;
    sockaddr_in addr_;
    std::mutex mtx_;

    char read_buf_[READ_BUFFER_SIZE];
    int read_idx_;

    char write_buf_[WRITE_BUFFER_SIZE];
    int write_idx_;

    struct iovec iov_[2];
    int iov_cnt_;

    int bytes_to_send_;
    int bytes_have_sent_;

    char* file_addr_;
    struct stat file_stat_;

    bool keep_alive_;
    std::string pending_headers_;

    HttpRequest request_;
    HttpResponse response_;

    static std::unordered_map<std::string, std::string> sessions_;
    static std::mutex sessions_mtx_;
    static std::atomic<unsigned long long> session_counter_;
};
