#pragma once

#include <string>
#include <unordered_map>

class HttpRequest
{
public:
    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH
    };

    enum class ParseResult {
        INCOMPLELE,
        SUCCESS,
        ERROR
    };

    enum class LineStatus {
        LINE_OK,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpRequest();

    void init();

    ParseResult parse(char* buf, int read_idx);

    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& version() const { return version_; }

    std::string getHeader(const std::string& key) const;
    bool isKeepAlive() const;

private:
    LineStatus parseLine(char* buf, int read_idx);
    bool parseRequestLine(char* text);
    bool parseHeader(char* text);

private:
    ParseState state_;

    int checked_idx_;
    int start_line_;

    std::string method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
};
