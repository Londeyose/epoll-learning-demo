#include "HttpConnection.h"
#include "LogMacro.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <sstream>

const char* HttpConnection::src_dir_ = "./resources";
std::unordered_map<std::string, std::string> HttpConnection::sessions_;
std::mutex HttpConnection::sessions_mtx_;
std::atomic<unsigned long long> HttpConnection::session_counter_(0);

HttpConnection::HttpConnection()
    : fd_(-1),
      read_idx_(0),
      write_idx_(0),
      iov_cnt_(0),
      bytes_to_send_(0),
      bytes_have_sent_(0),
      file_addr_(nullptr),
      keep_alive_(false) {
    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&addr_, 0, sizeof(addr_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));
}

HttpConnection::~HttpConnection() {
    closeConn();
}

void HttpConnection::init(int fd, const sockaddr_in& addr) {
    fd_ = fd;
    addr_ = addr;

    read_idx_ = 0;
    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;
    keep_alive_ = false;
    pending_headers_.clear();

    file_addr_ = nullptr;
    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));

    request_.init();
    response_.init();
}

void HttpConnection::closeConn() {
    unmapFile();
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

HttpConnection::HttpReadResult HttpConnection::read() {
    while (true) {
        int remain = READ_BUFFER_SIZE - read_idx_ - 1;
        if (remain <= 0) {
            return HttpReadResult::ERROR;
        }

        ssize_t len = ::read(fd_, read_buf_ + read_idx_, remain);
        if (len > 0) {
            read_idx_ += static_cast<int>(len);
            read_buf_[read_idx_] = '\0';
        } else if (len == 0) {
            return HttpReadResult::PEER_CLOSE;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            return HttpReadResult::ERROR;
        }
    }
    return HttpReadResult::DONE;
}

bool HttpConnection::process() {
    response_.init();
    unmapFile();
    pending_headers_.clear();

    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;
    LOG_DEBUG("Client(fd = %d)'s data = {%s}", fd_, read_buf_);
    auto ret = request_.parse(read_buf_, read_idx_);
    if (ret == HttpRequest::ParseResult::INCOMPLELE) {
        LOG_INFO("Client(fd = %d) send data are incomplete.", fd_);
        return false;
    }

    if (ret == HttpRequest::ParseResult::ERROR) {
        LOG_ERROR("Cant parse the data from client(fd=%d)", fd_);
        keep_alive_ = false;
        return prepareErrorResponse();
    }

    keep_alive_ = request_.isKeepAlive();

    if (!makeResponse()) {
        return false;
    }

    return prepareWrite();
}

bool HttpConnection::makeResponse() {
    response_.setExtraHeaders(pending_headers_);
    if (request_.method() == "GET" && request_.path() == "/whoami") {
        return handleWhoAmI();
    }

    if (request_.method() == "POST") {
        if (request_.path() == "/login") {
            return handleLogin();         
        }
        if (request_.path() == "/register") {
            return handleRegister();
        }
    }
    return response_.build(request_, src_dir_, keep_alive_);
}

bool HttpConnection::handleLogin() {
    std::string username = request_.getPost("username");
    std::string password = request_.getPost("password");
    LOG_DEBUG("handleLogin: name: %s, password: %s.", username.c_str(), password.c_str());

    std::string dbpassword;
    bool login_ok = false;
    {
        MYSQL* sql = nullptr;
        SqlConnRAII raii(&sql, SqlConnPool::instance());
        std::string sql_cmd = "select password from user where name = '";
        sql_cmd += username;
        sql_cmd += "'";

        if (mysql_query(sql, sql_cmd.c_str()) != 0) {
            LOG_ERROR("mysql_query failed: %s", mysql_error(sql));
            response_.makeErrorResponse(500, "Server Error", "<html><body><h1>Fail to query database.</h1></body></html>");
            return true;
        }

        MYSQL_RES* res = mysql_store_result(sql);
        if (!res) {
            LOG_ERROR("mysql_store_result failed: %s", mysql_error(sql));
            response_.makeErrorResponse(500, "Server Error", "<html><body><h1>Fail to query database.</h1></body></html>");
            return true;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
            dbpassword = row[0];
            login_ok = (dbpassword == password);
        }
        LOG_DEBUG("dbpassword = %s.", dbpassword.c_str());
        mysql_free_result(res);
    }
    if (login_ok) {
        LOG_INFO("LOGIN SUCCESS.");
        std::string session_id = generateSessionId(username);
        {
            std::lock_guard<std::mutex> lock(sessions_mtx_);
            sessions_[session_id] = username;
        }
        pending_headers_ = "Set-Cookie: session_id=" + session_id + "; Path=/; HttpOnly\r\n";
        response_.setExtraHeaders(pending_headers_);
        request_.setPath("/index.html");
    } else {
        LOG_INFO("LOGIN ERROR.");
        request_.setPath("/login.html");
    }
    return response_.build(request_, src_dir_, keep_alive_);

}

bool HttpConnection::handleRegister() {
    std::string username = request_.getPost("username");
    std::string password = request_.getPost("password");
    LOG_DEBUG("handleRegister: name: %s, password: %s.", username.c_str(), password.c_str());

    bool register_ok = false;
    {
        MYSQL* sql = nullptr;
        SqlConnRAII raii(&sql, SqlConnPool::instance());

        std::string check_sql = "select name from user where name = '";
        check_sql += username;
        check_sql += "'";
        if (mysql_query(sql, check_sql.c_str()) != 0) {
            LOG_ERROR("mysql_query failed when checking user: %s", mysql_error(sql));
            response_.makeErrorResponse(500, "Server Error", "<html><body><h1>Fail to query database.</h1></body></html>");
            return true;
        }

        MYSQL_RES* res = mysql_store_result(sql);
        if (!res) {
            LOG_ERROR("mysql_store_result failed when checking user: %s", mysql_error(sql));
            response_.makeErrorResponse(500, "Server Error", "<html><body><h1>Fail to query database.</h1></body></html>");
            return true;
        }

        bool user_exists = (mysql_fetch_row(res) != nullptr);
        mysql_free_result(res);
        if (!user_exists) {
            std::string insert_sql = "insert into user(name, password, score) values('";
            insert_sql += username;
            insert_sql += "', '";
            insert_sql += password;
            insert_sql += "', 0)";

            if (mysql_query(sql, insert_sql.c_str()) != 0) {
                LOG_ERROR("mysql_query failed when insert user: %s", mysql_error(sql));
                response_.makeErrorResponse(500, "Server Error", "<html><body><h1>Fail to insert user.</h1></body></html>");
                return true;
            }
            register_ok = true;
        }
    }

    if (register_ok) {
        LOG_INFO("REGISTER SUCCESS.");
        request_.setPath("/login.html");
    } else {
        LOG_INFO("REGISTER ERROR.");
        request_.setPath("/register.html");
    }
    return response_.build(request_, src_dir_, keep_alive_);
}

bool HttpConnection::handleWhoAmI() {
    std::string cookie = request_.getHeader("Cookie");
    std::string session_id = getCookieValue(cookie, "session_id");
    std::string username;

    if (!session_id.empty()) {
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            username = it->second;
        }
    }

    if (username.empty()) {
        response_.makeTextResponse(200, "application/json",
            "{\"ok\":false,\"username\":\"\"}", keep_alive_);
    } else {
        response_.makeTextResponse(200, "application/json",
            "{\"ok\":true,\"username\":\"" + username + "\"}", keep_alive_);
    }
    return true;
}

bool HttpConnection::prepareErrorResponse() {
    static const char* bad_request =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Content-Length: 50\r\n"
        "\r\n"
        "<html><body><h1>400 Bad Request</h1></body></html>";

    int len = static_cast<int>(std::strlen(bad_request));
    if (len >= WRITE_BUFFER_SIZE) {
        return false;
    }

    std::memcpy(write_buf_, bad_request, len);
    write_idx_ = len;

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = write_idx_;
    iov_cnt_ = 1;

    bytes_to_send_ = write_idx_;
    bytes_have_sent_ = 0;
    return true;
}

bool HttpConnection::prepareWrite() {
    const std::string& resp = response_.buffer();
    if (static_cast<int>(resp.size()) >= WRITE_BUFFER_SIZE) {
        return false;
    }

    std::memcpy(write_buf_, resp.data(), resp.size());
    write_idx_ = static_cast<int>(resp.size());

    iov_[0].iov_base = write_buf_;
    iov_[0].iov_len = write_idx_;
    iov_cnt_ = 1;

    if (response_.statusCode() == 200 && !response_.filePath().empty()) {
        if (!mapFile(response_.filePath())) {
            return false;
        }

        iov_[1].iov_base = file_addr_;
        iov_[1].iov_len = file_stat_.st_size;
        iov_cnt_ = 2;
        bytes_to_send_ = write_idx_ + static_cast<int>(file_stat_.st_size);
    } else {
        bytes_to_send_ = write_idx_;
    }

    bytes_have_sent_ = 0;
    return true;
}

HttpConnection::HttpWriteResult HttpConnection::write() {
    if (bytes_to_send_ <= 0) {
        return HttpWriteResult::DONE;
    }

    while (true) {
        ssize_t len = ::writev(fd_, iov_, iov_cnt_);
        if (len < 0) {
            if (errno == EAGAIN) {
                return HttpWriteResult::AGAIN;
            }
            if (errno == EINTR) {
                continue;
            }
            unmapFile();
            return HttpWriteResult::ERROR;
        }

        bytes_have_sent_ += static_cast<int>(len);
        bytes_to_send_ -= static_cast<int>(len);

        int sent = static_cast<int>(len);

        // 只保留你现在已有的“部分写偏移更新”思路
        if (iov_cnt_ >= 1) {
            if (sent >= static_cast<int>(iov_[0].iov_len)) {
                sent -= static_cast<int>(iov_[0].iov_len);
                iov_[0].iov_len = 0;

                if (iov_cnt_ == 2) {
                    iov_[1].iov_base = static_cast<char*>(iov_[1].iov_base) + sent;
                    iov_[1].iov_len -= sent;
                }
            } else {
                iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + sent;
                iov_[0].iov_len -= sent;
            }
        }

        if (bytes_to_send_ <= 0) {
            unmapFile();
            return HttpWriteResult::DONE;
        }
    }
}

bool HttpConnection::mapFile(const std::string& path) {
    int filefd = ::open(path.c_str(), O_RDONLY);
    if (filefd < 0) {
        return false;
    }

    if (::stat(path.c_str(), &file_stat_) < 0) {
        ::close(filefd);
        return false;
    }

    file_addr_ = static_cast<char*>(
        ::mmap(nullptr, file_stat_.st_size, PROT_READ, MAP_PRIVATE, filefd, 0));
    ::close(filefd);

    if (file_addr_ == MAP_FAILED) {
        file_addr_ = nullptr;
        return false;
    }
    return true;
}

void HttpConnection::unmapFile() {
    if (file_addr_) {
        ::munmap(file_addr_, file_stat_.st_size);
        file_addr_ = nullptr;
    }
}

void HttpConnection::reset() {
    unmapFile();

    read_idx_ = 0;
    write_idx_ = 0;
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    bytes_have_sent_ = 0;
    keep_alive_ = false;
    pending_headers_.clear();

    std::memset(read_buf_, 0, sizeof(read_buf_));
    std::memset(write_buf_, 0, sizeof(write_buf_));
    std::memset(&file_stat_, 0, sizeof(file_stat_));

    request_.init();
    response_.init();
}

std::string HttpConnection::generateSessionId(const std::string& username) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto seq = ++session_counter_;
    const auto user_hash = std::hash<std::string>{}(username);

    std::ostringstream oss;
    oss << std::hex << now << seq << user_hash;
    return oss.str();
}

std::string HttpConnection::getCookieValue(const std::string& cookie_header, const std::string& key) {
    std::string target = key + "=";
    size_t pos = cookie_header.find(target);
    if (pos == std::string::npos) {
        return "";
    }

    pos += target.size();
    size_t end = cookie_header.find(';', pos);
    if (end == std::string::npos) {
        end = cookie_header.size();
    }

    while (pos < end && cookie_header[pos] == ' ') {
        ++pos;
    }
    while (end > pos && cookie_header[end - 1] == ' ') {
        --end;
    }
    return cookie_header.substr(pos, end - pos);
}
