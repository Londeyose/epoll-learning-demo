#pragma once

#include <sys/uio.h>

#include <cstddef>
#include <string>

using std::string;

enum class ParseState { REQUEST_LINE, HEADERS, BODY, FINISH };

enum class LineStatus { LINE_OK, LINE_BAD, LINE_OPEN };

enum class HttpParseResult { OK, INVALID_METHOD, INVALID_VERSION, INVALID_REQUEST, INCOMPLETE };

enum class HttpReadResult { DONE, PEER_CLOSE, ERROR };

enum class HttpWriteResult { DONE, AGAIN, ERROR };

class HttpConnection {
public:
    HttpConnection(int fd);
    ~HttpConnection();

    HttpReadResult read();
    HttpWriteResult write();

    bool process();
    bool process_request();

    void close();
    int getfd();

    string get_method();
    string get_url();
    string get_version();

    void reset();
    bool is_keep_alive() const;
    void prepare_for_next_request();

private:
    LineStatus parse_line();
    HttpParseResult process_read();
    HttpParseResult parse_request_line(char* text);
    HttpParseResult parse_headers(char* text);

    bool add_response(const char* format, ...);
    void build_ok_response(int code, size_t file_size);
    void build_error_response(int code, const char* reason);
    static const char* get_content_type(const string& url);

    void reset_request_state();
    void reset_response_state();
    void compact_read_buffer();

    void adjust_iov(struct iovec* iov, int iovcnt, int sent);

private:
    static constexpr int READ_BUFFER_SIZE = 2048;
    static constexpr int WRITE_BUFFER_SIZE = 2048;

    int fd_;
    size_t read_idx_;
    size_t write_idx_;
    char read_buf_[READ_BUFFER_SIZE];
    char write_buf_[WRITE_BUFFER_SIZE];

    size_t checked_idx_;
    size_t start_line_;

    ParseState parse_state_;

    string method_;
    string url_;
    string version_;
    string host_;

    size_t content_length_;
    bool keep_alive_;

    void* file_addr_ = nullptr;
    size_t file_size_ = 0;
    size_t head_sent_ = 0;
    size_t file_sent_ = 0;
};
