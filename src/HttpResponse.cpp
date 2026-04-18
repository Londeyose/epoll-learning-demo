#include "HttpResponse.h"

#include <sys/stat.h>
#include <unistd.h>

HttpResponse::HttpResponse() {
    init();
}

void HttpResponse::init() {
    status_code_ = 200;
    keep_alive_ = false;
    file_path_.clear();
    buffer_.clear();
    extra_headers_.clear();
}

bool HttpResponse::build(const HttpRequest& request, const std::string& src_dir, bool keep_alive) {
    status_code_ = 200;
    keep_alive_ = keep_alive;
    buffer_.clear();
    file_path_ = src_dir + request.path();

    struct stat st {};
    if (stat(file_path_.c_str(), &st) < 0 || S_ISDIR(st.st_mode)) {
        makeErrorResponse(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
        return false;
    }

    if (!(st.st_mode & S_IROTH)) {
        makeErrorResponse(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
        return false;
    }

    status_code_ = 200;
    addStatusLine(200);
    addHeaders(static_cast<size_t>(st.st_size));
    return true;
}

void HttpResponse::makeErrorResponse(int status, const std::string& title, const std::string& text) {
    (void)title;
    status_code_ = status;
    file_path_.clear();

    addStatusLine(status);
    addContentType();
    addConnection();
    if (!extra_headers_.empty()) {
        buffer_ += extra_headers_;
    }
    addContentLength(text.size());
    addBlankLine();
    addContent(text);
}

void HttpResponse::setExtraHeaders(const std::string& headers) {
    extra_headers_ = headers;
}

void HttpResponse::makeTextResponse(int status, const std::string& content_type, const std::string& text, bool keep_alive) {
    init();
    status_code_ = status;
    keep_alive_ = keep_alive;

    addStatusLine(status);
    buffer_ += "Content-Type: " + content_type + "\r\n";
    addConnection();
    if (!extra_headers_.empty()) {
        buffer_ += extra_headers_;
    }
    addContentLength(text.size());
    addBlankLine();
    addContent(text);
}

void HttpResponse::addStatusLine(int status) {
    switch (status) {
    case 200:
        buffer_ += "HTTP/1.1 200 OK\r\n";
        break;
    case 403:
        buffer_ += "HTTP/1.1 403 Forbidden\r\n";
        break;
    case 404:
        buffer_ += "HTTP/1.1 404 Not Found\r\n";
        break;
    case 400:
        buffer_ += "HTTP/1.1 400 Bad Request\r\n";
        break;
    default:
        buffer_ += "HTTP/1.1 500 Internal Server Error\r\n";
        break;
    }
}

void HttpResponse::addHeaders(size_t content_len) {
    addContentType();
    addConnection();
    if (!extra_headers_.empty()) {
        buffer_ += extra_headers_;
    }
    addContentLength(content_len);
    addBlankLine();
}

void HttpResponse::addContentType() {
    buffer_ += "Content-Type: " + getMimeType(file_path_) + "\r\n";
}

void HttpResponse::addConnection() {
    if (keep_alive_) {
        buffer_ += "Connection: keep-alive\r\n";
    } else {
        buffer_ += "Connection: close\r\n";
    }
}

void HttpResponse::addContentLength(size_t len) {
    buffer_ += "Content-Length: " + std::to_string(len) + "\r\n";
}

void HttpResponse::addBlankLine() {
    buffer_ += "\r\n";
}

void HttpResponse::addContent(const std::string& content) {
    buffer_ += content;
}

std::string HttpResponse::getMimeType(const std::string& path) const {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "text/plain";

    std::string ext = path.substr(dot);
    if (ext == ".html") return "text/html";
    if (ext == ".css")  return "text/css";
    if (ext == ".js")   return "application/javascript";
    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".json") return "application/json";
    if (ext == ".txt")  return "text/plain";

    return "application/octet-stream";
}
