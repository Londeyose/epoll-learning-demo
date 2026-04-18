#pragma once

#include <mysql/mysql.h>
#include "SqlConnPool.h"

class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool* connPool) { 
        *sql = connPool->getConn();
        sql_ = *sql;
        connPool_ = connPool;
    }

    ~SqlConnRAII() {
        if (sql_) {
            connPool_->freeConn(sql_);
        }
    }

private:
    MYSQL* sql_;
    SqlConnPool* connPool_;
};