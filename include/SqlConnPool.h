#pragma once

#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>

class SqlConnPool {
public:
    static SqlConnPool* instance();

    void init(const char* host, int port, const char* user, const char* pwd, const char* dbName, int connSize);

    MYSQL* getConn();
    void freeConn(MYSQL* conn);
    void closePool();

    int getFreeConnCount();

private:
    SqlConnPool() = default;
    ~SqlConnPool();

    int MAX_COUNT_ = 0;
    int use_count_ = 0;
    int free_count_ = 0;

    std::queue<MYSQL*> conn_queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};