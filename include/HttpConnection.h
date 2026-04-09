#pragma once

#include <sys/uio.h>
#include <cstddef>
#include <string>

using std::string;

enum class HttpParseResult { OK, INVALID_METHOD, INVALID_VERSION, INVALID_REQUEST };

enum class HttpReadResult { DONE, PEER_CLOSE, ERROR };

enum class HttpWriteResult { DONE, AGAIN, ERROR };

class HttpConnection {
public:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;

    HttpConnection(int fd);
    ~HttpConnection();

    // server call this function to read data from client!
    HttpReadResult read();

    // server call this to process the content which sent from client
    bool process();

    // server call this to send data to client;
    HttpWriteResult write();

    void close();

    int getfd();

    string get_method();
    string get_url();
    string get_version();

    void reset();

    static const char* get_content_type(const string& url);

private:
    int fd_;
    int read_idx_;
    int write_idx_;
    char read_buf_[READ_BUFFER_SIZE];
    char write_buf_[WRITE_BUFFER_SIZE];

    string method_;
    string url_;
    string version_;
    void* file_addr_ = nullptr;
    size_t file_size_ = 0;
    size_t head_sent_ = 0;
    size_t file_sent_ = 0;

    HttpParseResult parse_request_line(char* text);
    bool add_response(const char* format, ...);
    void build_ok_response(int code, size_t file_size);
    void build_error_response(int code, const char* reason);
    void adjust_iov(struct iovec* iov, int iovcnt, int sent);
};
