#include "SqlConnPool.h"

#include <cassert>

SqlConnPool* SqlConnPool::instance() {
    static SqlConnPool pool;
    return &pool;
}

void SqlConnPool::init(const char* host, int port, const char* user, const char* pwd, const char* dbName, int connSize) {
    std::lock_guard<std::mutex> lock(mtx_);

    for (int i = 0; i < connSize; ++i) {
        MYSQL* sql = nullptr;
        sql = mysql_init(sql);
        assert(sql);

        sql = mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
        assert(sql);

        conn_queue_.push(sql);
    }

    MAX_COUNT_ = connSize;
    free_count_ = connSize;
}

MYSQL* SqlConnPool::getConn() {
    std::unique_lock<std::mutex> lock(mtx_);

    while (conn_queue_.empty()) {
        cv_.wait(lock);
    }

    MYSQL* sql = conn_queue_.front();
    conn_queue_.pop();

    --free_count_;
    ++use_count_;

    return sql;
}

void SqlConnPool::freeConn(MYSQL* conn) {
    if (conn == nullptr) return;

    std::lock_guard<std::mutex> lock(mtx_);
    conn_queue_.push(conn);
    ++free_count_;
    --use_count_;
    cv_.notify_one();
}

void SqlConnPool::closePool() {
    std::lock_guard<std::mutex> lock(mtx_);

    while (!conn_queue_.empty()) {
        MYSQL* sql = conn_queue_.front();
        conn_queue_.pop();
        mysql_close(sql);
    }

    use_count_ = 0;
    free_count_ = 0;
}

int SqlConnPool::getFreeConnCount() {
    std::lock_guard<std::mutex> lock(mtx_);
    return free_count_;
}

SqlConnPool::~SqlConnPool() {
    closePool();
}
