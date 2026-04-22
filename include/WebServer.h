#pragma once

#include "HttpConnection.h"
#include "Epoller.h"
#include "HeapTimer.h"
#include "ThreadPool.h"
#include <queue>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
class WebServer {
public:
    WebServer(int port, int trig_mode = 1, int timeout_ms = -1, int thread_num = 8);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool init();
    void start();

private:
    bool initListenSocket();
    void eventLoop();

    void handleListen();
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleWakeup();
    void closeConn(int fd);

    struct AsyncProcessResult {
        int fd;
        bool ready_to_write;
    };

private:
    int port_;
    int timeout_ms_;
    int thread_num_;
    int server_fd_;
    int wakeup_fd_;
    bool is_running_;

    ThreadPool thread_pool_;
    Epoller epoller_;
    HeapTimer timer_;
    std::unordered_map<int, std::shared_ptr<HttpConnection>> user_;
    std::unordered_set<int> processing_fds_;
    std::queue<AsyncProcessResult> completed_results_;
    std::mutex completed_mtx_;
};
