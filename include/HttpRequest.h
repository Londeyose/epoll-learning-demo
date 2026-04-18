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
    void setPath(const std::string& path) { path_ = path; }

    const std::unordered_map<std::string, std::string>& post() const;
    std::string getHeader(const std::string& key) const;
    std::string getPost(const std::string& key) const;    

    bool isKeepAlive() const;
    void reset();

private:
    void parseBody(char* buf, int read_idx);
    void parseFormUrlencoded();
    static std::string urlDecode(const std::string& str);
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
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> post_;
};
