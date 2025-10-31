#include "Server.h"
#include "Robot.h"
#include "Map.h"
#include "Logger.h"
#include "Module.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <regex>
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include "plugins/PluginAPI.h"
#include "ModuleManager.h"
#include <algorithm>
#include <unordered_set>

static void host_register_impl(void *host_ctx, const char *moduleId, plugin_callback_fn cb)
{
    if (!moduleId || !cb)
        return;
    std::string id(moduleId);
    ModuleManager::instance().registerCallback(id, [cb](const std::string &ctx)
                                               { cb(ctx.c_str()); });
}

static void host_unregister_impl(void *host_ctx, const char *moduleId)
{
    if (!moduleId)
        return;
    ModuleManager::instance().unregisterCallback(moduleId);
}

static void host_log_impl(void *host_ctx, int level, const char *msg)
{
    if (!msg)
        return;
    const char *lvl = "INFO";
    switch (level)
    {
    case 1:
        lvl = "WARN";
        break;
    case 2:
        lvl = "ERROR";
        break;
    case 3:
        lvl = "DEBUG";
        break;
    default:
        lvl = "INFO";
        break;
    }
    std::cerr << "[plugin-host-" << lvl << "] " << msg << std::endl;
}

Server::Server(int port) : port(port), running(false)
{
    // default logger
    logger = makeConsoleLogger();

    // prepare host API
    hostApi.host_ctx = this;
    hostApi.register_callback = &host_register_impl;
    hostApi.unregister_callback = &host_unregister_impl;
    hostApi.log = &host_log_impl;
}

Server::Server(int port, std::unique_ptr<Logger> inLogger) : port(port), running(false), logger(std::move(inLogger)) {}

Server::~Server()
{
    stop();
}

std::unordered_map<std::string, Robot> robots;
std::unordered_map<std::string, Map> maps;
std::unordered_map<std::string, Module> modules;

// Helper function to extract body from HTTP request
std::string extractBody(const std::string &request)
{
    size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart != std::string::npos)
    {
        return request.substr(bodyStart + 4);
    }

    bodyStart = request.find("\n\n");
    if (bodyStart != std::string::npos)
    {
        return request.substr(bodyStart + 2);
    }

    return "";
}

void Server::initializeHandlers()
{
    // Expose available plugins to clients
    registerEndpoint("GET /plugins", [this](const std::string &request)
                     {
        std::ostringstream out;
        out << "[";
        if (!pluginsDirectory.empty()) {
            DIR* dir = opendir(pluginsDirectory.c_str());
            if (dir) {
                struct dirent* ent;
                bool first = true;
                while ((ent = readdir(dir)) != nullptr) {
                    std::string name = ent->d_name;
                    if (name.size() > 3 && name.substr(name.size()-3) == ".so") {
                        std::string id = name.substr(0, name.size()-3);
                        if (!first) out << ",";
                        out << '"' << id << '"';
                        first = false;
                    }
                }
                closedir(dir);
            }
        }
        out << "]";
        return out.str(); });

    // Get currently enabled plugins
    registerEndpoint("GET /enabled-plugins", [this](const std::string &request)
                     {
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const auto &id : enabledPlugins) {
            if (!first) out << ",";
            out << '"' << id << '"';
            first = false;
        }
        out << "]";
        return out.str(); });

    // Set enabled plugins (accept a JSON array of strings in the body)
    registerEndpoint("POST /enabled-plugins", [this](const std::string &request)
                     {
        std::string body = extractBody(request);
        std::unordered_set<std::string> newSet;
        std::regex re("\"([^\"]+)\"");
        std::smatch m;
        std::string s = body;
        while (std::regex_search(s, m, re)) {
            newSet.insert(m[1]);
            s = m.suffix();
        }
        enabledPlugins = std::move(newSet);
        if (logger) logger->log(LogLevel::Info, "Updated enabled plugins, count=" + std::to_string(enabledPlugins.size()));
        return std::string("Enabled plugins updated\n"); });

    // Invoke a plugin by id (only if enabled)
    registerEndpoint("POST /invoke/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);
        std::regex idRegex("/invoke/([^/]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (enabledPlugins.find(id) == enabledPlugins.end()) {
                return std::string("Plugin not enabled\n");
            }
            bool ok = ModuleManager::instance().invoke(id, body);
            return ok ? std::string("Invoked\n") : std::string("Plugin not found\n");
        }
        return std::string("Bad request\n"); });

    registerEndpoint("POST /robots/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        Robot newRobot = Robot::deserialize(body);
        if (std::regex_search(path, match, idRegex)) {
            newRobot.id = match[1];
        }
        robots[newRobot.id] = newRobot;

        if (logger) logger->log(LogLevel::Info, std::string("Created robot id=") + newRobot.id);
        return std::string("Robot created successfully\n"); });

    registerEndpoint("POST /robots", [this](const std::string &request)
                     {
        std::string body = extractBody(request);

        std::vector<Robot> newRobots = Robot::deserializeList(body);
        for (const auto& robot : newRobots) {
            robots[robot.id] = robot;
        }

        if (logger) logger->log(LogLevel::Info, "Created " + std::to_string(newRobots.size()) + " robots");
        return std::string("Robots created successfully\n"); });

    registerEndpoint("PATCH /robots/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (robots.find(id) != robots.end()) {
                robots[id] = Robot::deserialize(body);
                if (logger) logger->log(LogLevel::Info, "Updated robot id=" + id);
                return std::string("Robot updated successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Patch robot not found");
        return std::string("Robot not found\n"); });

    registerEndpoint("GET /robots", [this](const std::string &request)
                     {
        std::ostringstream response;
        response << "[";
        for (const auto& [id, robot] : robots) {
            response << robot.serialize() << ",";
        }
        std::string result = response.str();
        if (!robots.empty()) {
            result.pop_back(); // Remove trailing comma
        }
        result += "]";

        if (logger) logger->log(LogLevel::Info, "Fetched all robots, count=" + std::to_string(robots.size()));
        return result; });

    registerEndpoint("GET /robots/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (robots.find(id) != robots.end()) {
                if (logger) logger->log(LogLevel::Info, "Fetched robot id=" + id);
                return std::string(robots[id].serialize());
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Get robot not found");
        return std::string("Robot not found\n"); });

    registerEndpoint("DELETE /robots/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (robots.erase(id)) {
                if (logger) logger->log(LogLevel::Info, "Deleted robot id=" + id);
                return std::string("Robot deleted successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Delete robot not found");
        return std::string("Robot not found\n"); });

    registerEndpoint("DELETE /robots", [this](const std::string &request)
                     {
        robots.clear();
        if (logger) logger->log(LogLevel::Info, "Deleted all robots");
        return std::string("All robots deleted successfully\n"); });

    registerEndpoint("POST /modules", [this](const std::string &request)
                     {
        std::string body = extractBody(request);
        std::vector<Module> newModules = Module::deserializeList(body);
        for (const auto &m : newModules) {
            modules[m.id] = m;
            if (logger) logger->log(LogLevel::Info, "Added module id=" + m.id + " name=" + m.name);
        }
        return std::string("Modules created\n"); });

    registerEndpoint("POST /modules/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);
        Module m = Module::deserialize(body);
        std::regex idRegex("/modules/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            m.id = match[1];
        }
        modules[m.id] = m;
        if (logger) logger->log(LogLevel::Info, "Added module id=" + m.id);
        return std::string("Module created\n"); });

    registerEndpoint("GET /modules", [this](const std::string &request)
                     {
        std::ostringstream out;
        out << "[";
        for (const auto &kv : modules) {
            out << kv.second.serialize() << ",";
        }
        std::string s = out.str();
        if (!modules.empty()) s.pop_back();
        s += "]";
        if (logger) logger->log(LogLevel::Info, "Fetched all modules, count=" + std::to_string(modules.size()));
        return s; });

    registerEndpoint("GET /modules/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::regex idRegex("/modules/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            auto it = modules.find(id);
            if (it != modules.end()) {
                if (logger) logger->log(LogLevel::Info, "Fetched module id=" + id);
                return it->second.serialize();
            }
        }
        if (logger) logger->log(LogLevel::Warn, "Module not found");
        return std::string("Module not found\n"); });

    registerEndpoint("PATCH /modules/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);
        std::regex idRegex("/modules/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            auto it = modules.find(id);
            if (it != modules.end()) {
                Module m = Module::deserialize(body);
                // Only update fields that are present in the body; naive replace for now
                if (!m.name.empty()) it->second.name = m.name;
                if (!m.description.empty()) it->second.description = m.description;
                it->second.enabled = m.enabled;
                if (logger) logger->log(LogLevel::Info, "Updated module id=" + id);
                return std::string("Module updated\n");
            }
        }
        if (logger) logger->log(LogLevel::Warn, "Module patch not found");
        return std::string("Module not found\n"); });

    registerEndpoint("DELETE /modules/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::regex idRegex("/modules/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (modules.erase(id)) {
                if (logger) logger->log(LogLevel::Info, "Deleted module id=" + id);
                return std::string("Module deleted\n");
            }
        }
        if (logger) logger->log(LogLevel::Warn, "Module delete not found");
        return std::string("Module not found\n"); });

    registerEndpoint("POST /map/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);
        if (logger) logger->log(LogLevel::Debug, std::string("Received map body: ") + body + std::string(" Path: ") + path + std::string(" Method: ") + method);

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            
            std::regex widthRegex("\"width\"\\s*:\\s*([0-9]+)");
            std::regex heightRegex("\"height\"\\s*:\\s*([0-9]+)");
            std::smatch widthMatch, heightMatch;

            if (std::regex_search(body, widthMatch, widthRegex) && std::regex_search(body, heightMatch, heightRegex)) {
                int width = std::stoi(widthMatch[1]);
                int height = std::stoi(heightMatch[1]);
                
                maps.emplace(id, Map(width, height));
                if (logger) logger->log(LogLevel::Info, "Created map with id=" + id + ", width=" + std::to_string(width) + ", height=" + std::to_string(height));

                return std::string("Map created successfully\n");
            } else {
                return std::string("Failed to parse map dimensions\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Failed to create map (bad path)");
        return std::string("Failed to create map\n"); });

    registerEndpoint("PATCH /map/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (maps.find(id) != maps.end()) {
                // Parse width and height from JSON body
                std::regex widthRegex("\"width\"\\s*:\\s*([0-9]+)");
                std::regex heightRegex("\"height\"\\s*:\\s*([0-9]+)");
                std::smatch widthMatch, heightMatch;

                if (std::regex_search(body, widthMatch, widthRegex) && std::regex_search(body, heightMatch, heightRegex)) {
                    int width = std::stoi(widthMatch[1]);
                    int height = std::stoi(heightMatch[1]);
                    
                    // Replace the map with a new one with updated dimensions
                    maps.erase(id);
                    maps.emplace(id, Map(width, height));
                    
                    if (logger) logger->log(LogLevel::Info, "Updated map id=" + id + ", width=" + std::to_string(width) + ", height=" + std::to_string(height));
                    return std::string("Map updated successfully\n");
                } else {
                    return std::string("Failed to parse map dimensions\n");
                }
            }
        }

        return std::string("Map not found\n"); });

    registerEndpoint("GET /map/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            auto it = maps.find(id);
            if (it != maps.end()) {
                const Map &m = it->second;
                std::ostringstream out;
                out << "{\"id\":\"" << id << "\",\"width\":" << m.getWidth() << ",\"height\":" << m.getHeight() << "}";
                if (logger) logger->log(LogLevel::Info, "Fetched map id=" + id);
                return out.str();
            }
        }
        if (logger) logger->log(LogLevel::Warn, "Get map not found");
        return std::string("Map not found\n"); });

    registerEndpoint("GET /map/", [this](const std::string &request)
                     {
        std::ostringstream response;
        response << "[";
        for (const auto& [id, map] : maps) {
            response << "{\"id\":\"" << id << "\""
                    << ",\"name\":\"Map " << id << "\""
                    << ",\"width\":" << map.getWidth() 
                    << ",\"height\":" << map.getHeight() 
                    << "},";
        }
        std::string result = response.str();
        if (!maps.empty()) {
            result.pop_back(); // Remove trailing comma
        }
        result += "]";

        return result; });

    registerEndpoint("DELETE /map/{id}", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (maps.erase(id)) {
                // Delete all robots that belong to this map
                int deletedRobots = 0;
                for (auto it = robots.begin(); it != robots.end();) {
                    if (it->second.mapId == id) {
                        if (logger) logger->log(LogLevel::Info, std::string("Deleted robot id=") + it->second.id + " (map cascade)");
                        it = robots.erase(it);
                        deletedRobots++;
                    } else {
                        ++it;
                    }
                }
                if (logger) logger->log(LogLevel::Info, std::string("Deleted map id=") + id + " and " + std::to_string(deletedRobots) + " associated robots");
                return std::string("Map deleted successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Delete map not found");
        return std::string("Map not found\n"); });

    // Endpoint to invoke pathfinding for a robot against a specific map
    // Expects JSON body: {"mapId":"<map-uuid>","target":[x,y]}
    registerEndpoint("POST /robots/{id}/pathfind", [this](const std::string &request)
                     {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::string body = extractBody(request);

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string robotId = match[1];
            auto rIt = robots.find(robotId);
            if (rIt == robots.end()) {
                if (logger) logger->log(LogLevel::Warn, "Pathfind: robot not found id=" + robotId);
                return std::string("Robot not found\n");
            }

            std::regex mapIdRe("\"mapId\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch m2;
            if (!std::regex_search(body, m2, mapIdRe)) {
                return std::string("mapId missing\n");
            }
            std::string mapId = m2[1];
            auto mIt = maps.find(mapId);
            if (mIt == maps.end()) {
                if (logger) logger->log(LogLevel::Warn, "Pathfind: map not found id=" + mapId);
                return std::string("Map not found\n");
            }

            std::regex tgtRe("\"target\"\\s*:\\s*\\[\\s*([0-9.+\-eE]+)\\s*,\\s*([0-9.+\-eE]+)\\s*\\]");
            std::smatch m3;
            if (!std::regex_search(body, m3, tgtRe)) {
                return std::string("target missing\n");
            }
            float tx = 0.0f;
            float ty = 0.0f;
            try {
                tx = std::stof(m3[1]);
                ty = std::stof(m3[2]);
            } catch (...) {
                return std::string("bad target values\n");
            }

            // Execute pathfinding (this will append to simulation.log)
            try {
                rIt->second.pathfind(mIt->second, std::vector<float>{tx, ty});
            } catch (const std::exception& ex) {
                if (logger) logger->log(LogLevel::Error, std::string("Pathfind exception: ") + ex.what());
                return std::string("Pathfind failed\n");
            }

            if (logger) logger->log(LogLevel::Info, "Pathfind executed for robot=" + robotId + " map=" + mapId);
            return std::string("Pathfind executed\n");
        }

        return std::string("Bad request\n"); });
}

void Server::start()
{
    initializeHandlers();
    running = true;
    serverThread = std::thread(&Server::run, this);
    if (logger)
        logger->log(LogLevel::Info, "Server started on port " + std::to_string(port));
}

void Server::stop()
{
    running = false;
    if (serverThread.joinable())
    {
        serverThread.join();
    }
    // unload any plugins that were loaded
    unloadPlugins();

    if (logger)
        logger->log(LogLevel::Info, "Server stopped.");
}

// Load all .so files in dirPath. For each plugin, call plugin_start(&hostApi, moduleId)
int Server::loadPluginsFromDirectory(const std::string &dirPath)
{
    // remember directory for listing
    pluginsDirectory = dirPath;
    DIR *dir = opendir(dirPath.c_str());
    if (!dir)
    {
        if (logger)
            logger->log(LogLevel::Warn, std::string("Failed to open plugins directory: ") + dirPath);
        return 0;
    }

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        std::string name = ent->d_name;
        if (name.size() > 3 && name.substr(name.size() - 3) == ".so")
        {
            std::string fullpath = dirPath + "/" + name;
            void *handle = dlopen(fullpath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle)
            {
                if (logger)
                    logger->log(LogLevel::Error, std::string("dlopen failed for ") + fullpath + ": " + dlerror());
                continue;
            }

            using start_fn_t = int (*)(const HostAPI *, const char *);
            using stop_fn_t = void (*)();

            dlerror(); // clear
            start_fn_t start = reinterpret_cast<start_fn_t>(dlsym(handle, "plugin_start"));
            const char *dlsym_err = dlerror();
            if (dlsym_err || !start)
            {
                if (logger)
                    logger->log(LogLevel::Warn, std::string("plugin_start not found in ") + fullpath);
                dlclose(handle);
                continue;
            }

            stop_fn_t stop = reinterpret_cast<stop_fn_t>(dlsym(handle, "plugin_stop"));
            // stop may be optional; still allow plugin to load

            // Use file base name (without .so) as moduleId
            std::string moduleId = name.substr(0, name.size() - 3);

            int rc = start(&hostApi, moduleId.c_str());
            if (rc != 0)
            {
                if (logger)
                    logger->log(LogLevel::Warn, std::string("plugin_start failed for ") + fullpath);
                if (stop)
                    stop();
                dlclose(handle);
                continue;
            }

            PluginEntry entry;
            entry.handle = handle;
            entry.stopFn = stop;
            entry.path = fullpath;
            entry.moduleId = moduleId;
            loadedPlugins.push_back(entry);
            ++loaded;
            if (logger)
                logger->log(LogLevel::Info, std::string("Loaded plugin: ") + fullpath + " as moduleId=" + moduleId);
        }
    }

    closedir(dir);
    return loaded;
}

// After initializeHandlers registration, expose endpoints to get/set enabled plugins

void Server::unloadPlugins()
{
    // Unregister and dlclose in reverse order
    for (auto it = loadedPlugins.rbegin(); it != loadedPlugins.rend(); ++it)
    {
        if (it->stopFn)
        {
            try
            {
                it->stopFn();
            }
            catch (...)
            {
            }
        }
        if (it->handle)
        {
            dlclose(it->handle);
            it->handle = nullptr;
        }
        if (logger)
            logger->log(LogLevel::Info, std::string("Unloaded plugin: ") + it->path);
    }
    loadedPlugins.clear();
}

void Server::registerEndpoint(const std::string &endpoint, std::function<std::string(const std::string &)> handler)
{
    endpointHandlers[endpoint] = handler;
}

void Server::run()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, 3) < 0)
    {
        close(server_fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    while (running)
    {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd < 0)
        {
            if (running && logger)
            {
                logger->log(LogLevel::Error, "Failed to accept connection");
            }
            continue;
        }

        if (logger)
            logger->log(LogLevel::Info, std::string("New client accepted from ") + inet_ntoa(client_address.sin_addr) + ":" + std::to_string(ntohs(client_address.sin_port)));

        // Read the full request (handle large payloads)
        std::string request;
        char buffer[4096] = {0};
        int bytes_read;

        while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0)
        {
            request.append(buffer, bytes_read);
            // Check if we've read the complete request
            if (request.find("\r\n\r\n") != std::string::npos || request.find("\n\n") != std::string::npos)
            {
                // Check if Content-Length header exists and if we've read enough
                size_t clPos = request.find("Content-Length:");
                if (clPos != std::string::npos)
                {
                    size_t clEnd = request.find("\r\n", clPos);
                    if (clEnd == std::string::npos)
                        clEnd = request.find("\n", clPos);
                    if (clEnd != std::string::npos)
                    {
                        int contentLength = std::atoi(request.substr(clPos + 15, clEnd - clPos - 15).c_str());
                        size_t bodyStart = request.find("\r\n\r\n");
                        if (bodyStart == std::string::npos)
                            bodyStart = request.find("\n\n");
                        if (bodyStart != std::string::npos)
                        {
                            bodyStart += (request[bodyStart + 2] == '\n' ? 2 : 4);
                            int currentBodySize = request.size() - bodyStart;
                            if (currentBodySize >= contentLength)
                            {
                                break; // Got full body
                            }
                        }
                    }
                }
                else
                {
                    // No Content-Length, assume request is complete
                    break;
                }
            }
            std::memset(buffer, 0, sizeof(buffer));
        }

        if (!request.empty())
        {
            if (logger)
                logger->log(LogLevel::Debug, "Received request: " + request);
            std::string response = handleRequest(request);
            send(client_fd, response.c_str(), response.size(), 0);
        }
        close(client_fd);
    }

    close(server_fd);
}

std::string Server::handleRequest(const std::string &request)
{
    std::istringstream requestStream(request);
    std::string method, path;
    requestStream >> method >> path;

    // Handle CORS preflight OPTIONS requests
    if (method == "OPTIONS")
    {
        std::ostringstream response;
        response << "HTTP/1.1 204 No Content\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
        response << "Access-Control-Max-Age: 86400\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        return response.str();
    }

    for (const auto &[endpoint, handler] : endpointHandlers)
    {
        auto spacePos = endpoint.find(' ');
        if (spacePos == std::string::npos)
            continue;
        std::string endpointMethod = endpoint.substr(0, spacePos);
        std::string endpointPath = endpoint.substr(spacePos + 1);

        if (endpointMethod != method)
            continue; // method must match exactly

        std::string endpointPattern = endpointPath;
        size_t pos;
        while ((pos = endpointPattern.find("{")) != std::string::npos)
        {
            size_t endPos = endpointPattern.find("}", pos);
            if (endPos != std::string::npos)
            {
                endpointPattern.replace(pos, endPos - pos + 1, "[^/]+");
            }
        }

        // Use regex to match the path only (not the method)
        std::regex pattern(endpointPattern);
        if (std::regex_match(path, pattern))
        {
            std::string body = handler(request);
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << body.size() << "\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n";
            response << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << body;
            return response.str();
        }
    }

    // Default to 404 if no match is found
    std::ostringstream response;
    response << "HTTP/1.1 404 Not Found\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: 13\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << "404 Not Found";
    return response.str();
}
