#include "HttpConnection.h"
#include "Epoller.h"

#include <unordered_map>
class WebServer {
public:
    WebServer(int port, int trig_mode = 1, int timeout_ms = -1);
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
    void closeConn(int fd);

private:
    int port_;
    int timeout_ms_;
    int server_fd_;
    bool is_running_;

    Epoller epoller_;
    std::unordered_map<int, HttpConnection> user_;
};