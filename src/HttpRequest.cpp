#include "HttpRequest.h"
#include "LogMacro.h"
#include <string.h>
HttpRequest::HttpRequest() { init(); }

void HttpRequest::init() { state_ = ParseState::REQUEST_LINE;
    checked_idx_ = 0;
    start_line_ = 0;

    method_.clear();
    version_.clear();
    path_.clear();
    headers_.clear();
}

HttpRequest::ParseResult HttpRequest::parse(char* buf, int read_idx) {
    if (buf == nullptr || read_idx <= 0) {
        return ParseResult::INCOMPLELE;
    }

    while (true) {
        LineStatus line_status = parseLine(buf, read_idx);
        if (line_status == LineStatus::LINE_OPEN) {
            return ParseResult::INCOMPLELE;
        }

        if (line_status == LineStatus::LINE_BAD) {
            return ParseResult::ERROR;
        }

        char* text = buf + start_line_;
        start_line_ = checked_idx_;

        switch (state_) {
        case ParseState::REQUEST_LINE:
            if (!parseRequestLine(text)) {
                return ParseResult::ERROR;
            }
            state_ = ParseState::HEADERS;
            break;
        case ParseState::HEADERS:
            if (text[0] == '\0') {
                state_ = ParseState::BODY;
                break;
            }

            if (!parseHeader(text)) {
                return ParseResult::ERROR;
            }
            break;
        case ParseState::BODY:
            parseBody(buf, read_idx);
            state_ = ParseState::FINISH;
            break;
        case ParseState::FINISH:
            return ParseResult::SUCCESS;
        default:
            return ParseResult::SUCCESS;
        }
    }
}

HttpRequest::LineStatus HttpRequest::parseLine(char* buf, int read_idx) {
    if (state_ == ParseState::BODY || state_ == ParseState::FINISH) {
        LOG_DEBUG("Cancel line check!");
        return LineStatus::LINE_OK;
    }
    for (; checked_idx_ < read_idx; checked_idx_ += 1) {
        char ch = buf[checked_idx_];

        if (ch == '\r') {
            if (checked_idx_ + 1 == read_idx) {
                return LineStatus::LINE_OPEN;
            }
            if (buf[checked_idx_ + 1] == '\n') {
                buf[checked_idx_] = 0;
                buf[checked_idx_ + 1] = 0;
                checked_idx_ += 2;
                return LineStatus::LINE_OK;
            }
            return LineStatus::LINE_BAD;
        }

        if (ch == '\n') {
            return LineStatus::LINE_BAD;
        }

    }
    return LineStatus::LINE_OPEN;
}

bool HttpRequest::parseRequestLine(char* text) {
    char* url = strpbrk(text, " \t");
    if (!url) return false;
    *url++ = '\0';

    while (*url == ' ' || *url == '\t') ++url;

    char* version = strpbrk(url, " \t");
    if (!version) return false;
    *version++ = '\0';

    while (*version == ' ' || *version == '\t') ++version;

    method_ = text;
    path_ = url;
    version_ = version;

    if (strcasecmp(method_.c_str(), "GET") != 0 &&
        strcasecmp(method_.c_str(), "POST") != 0) {
        return false;
    }

    if (strcasecmp(version_.c_str(), "HTTP/1.1") != 0) {
        return false;
    }

    if (path_.empty()) return false;

    if (path_ == "/") {
        path_ = "/login.html";
    }
    LOG_INFO("Request line parsed: method=%s, path=%s, version=%s",
             method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

bool HttpRequest::parseHeader(char* text) {
    char* colon = strpbrk(text, ":");
    if (!colon) return false;

    *colon++ = '\0';
    while (*colon == ' ' || *colon == '\t') ++colon;
    headers_[text] = colon;
    return true;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    if (it == headers_.end()) return "";
    return it->second;
}

bool HttpRequest::isKeepAlive() const {
    auto it = headers_.find("Connection");
    if (it == headers_.end()) {
        return false;
    }
    return strcasecmp(it->second.c_str(), "keep-alive") == 0;
}

void HttpRequest::parseBody(char* buf, int read_idx) {
    LOG_DEBUG("Parsing body.");
    post_.clear();

    if (method_ != "POST") {
        return;
    }

    auto it = headers_.find("Content-Type");
    if (it == headers_.end()) {
        return;
    }

    int content_len = 0;
    std::string len_str = getHeader("Content-Length");
    if (!len_str.empty()) {
        content_len = std::atoi(len_str.c_str());
        if (content_len < 0) return;
    }
    LOG_DEBUG("len = %d, body = %s.",content_len, body_.c_str());
    body_.assign(buf + checked_idx_, content_len);

    const std::string& content_type = it->second;
    
    if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
        parseFormUrlencoded();
    }
}

void HttpRequest::parseFormUrlencoded() {
    if (body_.empty()) return;

    size_t i = 0;
    size_t n = body_.size();

    while (i < n) {
        size_t key_end = body_.find('=', i);
        if (key_end == std::string::npos) break;

        size_t val_end = body_.find('&', key_end + 1);

        std::string key = body_.substr(i, key_end - i);
        std::string val;

        if (val_end == std::string::npos) {
            val = body_.substr(key_end + 1);
            i = n;
        } else {
            val = body_.substr(key_end + 1, val_end - key_end - 1);
            i = val_end + 1;
        }
        LOG_DEBUG("add key=%s, value=%s", key.c_str(), val.c_str());
        post_[urlDecode(key)] = urlDecode(val);
    }
}

std::string HttpRequest::urlDecode(const std::string& str) {
    std::string res;
    res.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '+') {
            res.push_back(' ');
        } else if (str[i] == '%' && i + 2 < str.size()) {
            auto hexToInt = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };

            int hi = hexToInt(str[i + 1]);
            int lo = hexToInt(str[i + 2]);

            if (hi != -1 && lo != -1) {
                res.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
            } else {
                res.push_back(str[i]);
            }
        } else {
            res.push_back(str[i]);
        }
    }

    return res;
}

std::string HttpRequest::getPost(const std::string& key) const {
    auto it = post_.find(key);
    if (it == post_.end()) {
        return "";
    }
    return it->second;
}

const std::unordered_map<std::string, std::string>& HttpRequest::post() const {
    return post_;
}

void HttpRequest::reset() {
    method_.clear();
    path_.clear();
    body_.clear();
    headers_.clear();
    post_.clear();
}
