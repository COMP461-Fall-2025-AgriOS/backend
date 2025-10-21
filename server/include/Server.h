#pragma once

#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <map>
#include "Logger.h"
#include <vector>
#include <memory>
#include <unordered_set>
#include "plugins/PluginAPI.h"

class Server {
public:
    Server(int port = 8080);
    Server(int port, std::unique_ptr<Logger> logger);
    ~Server();

    void start();
    void stop();

    void registerEndpoint(const std::string& endpoint, std::function<std::string(const std::string&)> handler);

    int loadPluginsFromDirectory(const std::string& dirPath);

private:
    int port;
    bool running;
    std::thread serverThread;
    std::map<std::string, std::function<std::string(const std::string&)>> endpointHandlers;
    std::unique_ptr<Logger> logger;

    void run();
    std::string handleRequest(const std::string& request);

    void initializeHandlers();

    void unloadPlugins();

private:
    struct PluginEntry {
        void* handle = nullptr; // dlopen handle
        void (*stopFn)() = nullptr;
        std::string path;
        std::string moduleId;
    };

    std::vector<PluginEntry> loadedPlugins;
    std::unordered_set<std::string> enabledPlugins;

    HostAPI hostApi;
    void* hostCtx = nullptr;

    std::string pluginsDirectory;
};