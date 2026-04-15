#pragma once

#include <string>

#include "HttpRequest.h"

class HttpResponse {
public:
    HttpResponse();

    void init();

    // src_dir: 资源根目录，例如 "/home/xxx/WebServer/resources"
    bool build(const HttpRequest& request, const std::string& src_dir, bool keep_alive);

    const char* data() const { return buffer_.c_str(); }
    int size() const { return static_cast<int>(buffer_.size()); }

    int statusCode() const { return status_code_; }
    bool keepAlive() const { return keep_alive_; }

    const std::string& filePath() const { return file_path_; }
    const std::string& buffer() const { return buffer_; }

private:
    void makeErrorResponse(int status, const std::string& title, const std::string& text);
    void addStatusLine(int status);
    void addHeaders(size_t content_len);
    void addContentType();
    void addConnection();
    void addContentLength(size_t len);
    void addBlankLine();
    void addContent(const std::string& content);

    std::string getMimeType(const std::string& path) const;

private:
    int status_code_;
    bool keep_alive_;
    std::string file_path_;
    std::string buffer_;
};