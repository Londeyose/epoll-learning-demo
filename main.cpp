#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "HttpConnection.h"
#include "Logger.h"
#include "LogMacro.h"
#include "Epoller.h"
#include "WebServer.h"


int main() {

    LogConfig config;
    config.log_dir = "logs";
    config.log_base_name = "WebServer";
    config.async_mode = true;
    config.queue_capacity = 2048;
    config.min_level = LogLevel::DEBUG;
    config.max_file_size = 10 * 1024 * 1024;

    config.flush_every_n_bytes = 64 * 1024;
    config.flush_every_n_message = 0;
    config.flush_internal_ms = 1000;

    LoggerGuard logger_guard(config);

    LOG_INFO("Logger initialize successfully");
    LOG_INFO("WebServer starting...");

    WebServer webserver(8888, 1, 10000);
    webserver.start();

    Logger::getInstance().stop();
    std::cout << "hello world!\n";
    return 0;
}
