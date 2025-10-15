#pragma once

#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <map>
#include "Logger.h"

class Server {
public:
    Server(int port = 8080);
    Server(int port, std::unique_ptr<Logger> logger);
    ~Server();

    void start();
    void stop();

    void registerEndpoint(const std::string& endpoint, std::function<std::string(const std::string&)> handler);

private:
    int port;
    bool running;
    std::thread serverThread;
    std::map<std::string, std::function<std::string(const std::string&)>> endpointHandlers;
    std::unique_ptr<Logger> logger;

    void run();
    std::string handleRequest(const std::string& request);

    void initializeHandlers();
};