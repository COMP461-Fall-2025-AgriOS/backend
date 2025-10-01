#pragma once

#include <string>
#include <thread>
#include <functional>
#include <map>

class Server {
public:
    Server(int port = 8080);
    ~Server();

    void start();
    void stop();

    void registerEndpoint(const std::string& endpoint, std::function<std::string(const std::string&)> handler);

private:
    int port;
    bool running;
    std::thread serverThread;
    std::map<std::string, std::function<std::string(const std::string&)>> endpointHandlers;

    void run();
    std::string handleRequest(const std::string& request);

    void initializeHandlers();
};