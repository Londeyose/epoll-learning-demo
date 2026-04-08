#pragma once

#include <string>

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

    std::string get_method();
    std::string get_url();
    std::string get_version();

    void reset();

private:
    int fd_;
    char read_buf_[READ_BUFFER_SIZE];
    int read_idx_;
    char write_buf_[WRITE_BUFFER_SIZE];
    int write_idx_;
    std::string method_;
    std::string url_;
    std::string version_;
    HttpParseResult parse_request_line(char* text);
    bool add_response(const char* format, ...);
};
