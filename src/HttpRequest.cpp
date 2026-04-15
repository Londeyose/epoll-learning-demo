#include "HttpRequest.h"

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
                state_ = ParseState::FINISH;
                return ParseResult::SUCCESS;
            }

            if (!parseHeader(text)) {
                return ParseResult::ERROR;
            }
            break;
        case ParseState::BODY:

            return ParseResult::ERROR;
        case ParseState::FINISH:
            return ParseResult::SUCCESS;
        default:
            return ParseResult::SUCCESS;
        }
    }
}

HttpRequest::LineStatus HttpRequest::parseLine(char* buf, int read_idx) {
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

    if (strcasecmp(method_.c_str(), "GET") != 0) {
        return false;
    }

    if (strcasecmp(version_.c_str(), "HTTP/1.1") != 0) {
        return false;
    }

    if (path_.empty()) return false;

    if (path_ == "/") {
        path_ = "/index.html";
    }

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