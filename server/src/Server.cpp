#include "Server.h"
#include "Robot.h"
#include "Map.h"
#include "TaskManager.h"
#include "Logger.h"
#include "Module.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cstdio>
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

static void host_register_impl(void* host_ctx, const char* moduleId, plugin_callback_fn cb) {
    if (!moduleId || !cb) return;
    std::string id(moduleId);
    ModuleManager::instance().registerCallback(id, [cb](const std::string& ctx){ cb(ctx.c_str()); });
}

static void host_unregister_impl(void* host_ctx, const char* moduleId) {
    if (!moduleId) return;
    ModuleManager::instance().unregisterCallback(moduleId);
}

static void host_log_impl(void* host_ctx, int level, const char* msg) {
    if (!msg) return;
    const char* lvl = "INFO";
    switch (level) {
        case 1: lvl = "WARN"; break;
        case 2: lvl = "ERROR"; break;
        case 3: lvl = "DEBUG"; break;
        default: lvl = "INFO"; break;
    }
    std::cerr << "[plugin-host-" << lvl << "] " << msg << std::endl;
}

Server::Server(int port) : port(port), running(false) {
    // default logger
    logger = makeConsoleLogger();

    // prepare host API
    hostApi.host_ctx = this;
    hostApi.register_callback = &host_register_impl;
    hostApi.unregister_callback = &host_unregister_impl;
    hostApi.log = &host_log_impl;
    
    // Set user plugins directory and create if needed
    userPluginsDirectory = "./plugins/user";
    system(("mkdir -p " + userPluginsDirectory).c_str());
}

Server::Server(int port, std::unique_ptr<Logger> inLogger) : port(port), running(false), logger(std::move(inLogger)) {
    // prepare host API
    hostApi.host_ctx = this;
    hostApi.register_callback = &host_register_impl;
    hostApi.unregister_callback = &host_unregister_impl;
    hostApi.log = &host_log_impl;
    
    // Set user plugins directory and create if needed
    userPluginsDirectory = "./plugins/user";
    system(("mkdir -p " + userPluginsDirectory).c_str());
}

Server::~Server() {
    stop();
}

std::unordered_map<std::string, Robot> robots;
std::unordered_map<std::string, std::unique_ptr<Map>> maps;
std::unordered_map<std::string, std::unique_ptr<TaskManager>> taskManagers; // mapId -> TaskManager
std::unordered_map<std::string, Module> modules;

// Helper function to extract body from HTTP request
std::string extractBody(const std::string& request) {
    size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        return request.substr(bodyStart + 4);
    }
    
    bodyStart = request.find("\n\n");
    if (bodyStart != std::string::npos) {
        return request.substr(bodyStart + 2);
    }
    
    return "";
}

void Server::initializeHandlers() {
    // Expose available plugins to clients
    registerEndpoint("GET /plugins", [this](const std::string& request) {
        std::ostringstream out;
        out << "[";
        bool first = true;
        
        // Scan main plugins directory
        if (!pluginsDirectory.empty()) {
            DIR* dir = opendir(pluginsDirectory.c_str());
            if (dir) {
                struct dirent* ent;
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
        
        // Scan user plugins directory
        DIR* userDir = opendir(userPluginsDirectory.c_str());
        if (userDir) {
            struct dirent* ent;
            while ((ent = readdir(userDir)) != nullptr) {
                std::string name = ent->d_name;
                if (name.size() > 3 && name.substr(name.size()-3) == ".so") {
                    std::string id = name.substr(0, name.size()-3);
                    if (!first) out << ",";
                    out << '"' << id << '"';
                    first = false;
                }
            }
            closedir(userDir);
        }
        
        out << "]";
        return out.str();
    });

    // Get currently enabled plugins
    registerEndpoint("GET /enabled-plugins", [this](const std::string& request) {
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const auto &id : enabledPlugins) {
            if (!first) out << ",";
            out << '"' << id << '"';
            first = false;
        }
        out << "]";
        return out.str();
    });

    // Set enabled plugins (accept a JSON array of strings in the body)
    registerEndpoint("POST /enabled-plugins", [this](const std::string& request) {
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
        return std::string("Enabled plugins updated\n");
    });

    // Invoke a plugin by id (only if enabled)
    registerEndpoint("POST /invoke/{id}", [this](const std::string& request) {
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
        return std::string("Bad request\n");
    });

    // Get plugin template
    registerEndpoint("GET /plugins/template", [this](const std::string& request) {
        std::string templateCode = 
            "#include \"plugins/PluginAPI.h\"\n"
            "#include <string>\n"
            "#include <cstring>\n\n"
            "static const HostAPI* g_api = nullptr;\n"
            "static std::string g_moduleId;\n\n"
            "static void plugin_callback(const char* context) {\n"
            "    const char* ctx = context ? context : \"\";\n"
            "    // TODO: Implement your plugin logic here\n"
            "    if (g_api && g_api->log) {\n"
            "        g_api->log(g_api->host_ctx, 0, \n"
            "            (std::string(\"Plugin: \") + g_moduleId + \" invoked with ctx=\" + ctx).c_str());\n"
            "    }\n"
            "}\n\n"
            "extern \"C\" int plugin_start(const HostAPI* api, const char* moduleId) {\n"
            "    if (!api || !moduleId) return -1;\n"
            "    g_api = api;\n"
            "    g_moduleId = moduleId;\n"
            "    if (g_api->register_callback) {\n"
            "        g_api->register_callback(g_api->host_ctx, g_moduleId.c_str(), &plugin_callback);\n"
            "    }\n"
            "    if (g_api->log) {\n"
            "        g_api->log(g_api->host_ctx, 0, \n"
            "            (std::string(\"Plugin started: \") + g_moduleId).c_str());\n"
            "    }\n"
            "    return 0;\n"
            "}\n\n"
            "extern \"C\" void plugin_stop() {\n"
            "    if (g_api && g_api->unregister_callback) {\n"
            "        g_api->unregister_callback(g_api->host_ctx, g_moduleId.c_str());\n"
            "    }\n"
            "    if (g_api && g_api->log) {\n"
            "        g_api->log(g_api->host_ctx, 0, \n"
            "            (std::string(\"Plugin stopped: \") + g_moduleId).c_str());\n"
            "    }\n"
            "    g_moduleId.clear();\n"
            "    g_api = nullptr;\n"
            "}\n";
        return templateCode;
    });

    // Get plugin source code
    registerEndpoint("GET /plugins/{id}/source", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::regex idRegex("/plugins/([^/]+)/source");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            std::string source = readPluginSource(id);
            if (source.empty()) {
                // Return empty body - frontend will treat this as no source
                return std::string("");
            }
            return source;
        }
        return std::string("");
    });

    // Save plugin source code
    registerEndpoint("POST /plugins/{id}/source", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);
        std::regex idRegex("/plugins/([^/]+)/source");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            bool success = savePluginSource(id, body);
            if (success) {
                if (logger) logger->log(LogLevel::Info, "Saved source for plugin: " + id);
                return std::string("Source saved successfully\n");
            } else {
                return std::string("HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to save source");
            }
        }
        return std::string("HTTP/1.1 400 Bad Request\r\n\r\nBad request");
    });

    // Compile plugin
    registerEndpoint("POST /plugins/{id}/compile", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::string body = extractBody(request);
        std::regex idRegex("/plugins/([^/]+)/compile");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            std::string result = compilePlugin(id, body);
            return result;
        }
        return std::string("HTTP/1.1 400 Bad Request\r\n\r\nBad request");
    });

    // Hot-load plugin
    registerEndpoint("POST /plugins/{id}/reload", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::regex idRegex("/plugins/([^/]+)/reload");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            bool success = hotLoadPlugin(id);
            if (success) {
                if (logger) logger->log(LogLevel::Info, "Hot-loaded plugin: " + id);
                return std::string("Plugin loaded successfully\n");
            } else {
                return std::string("HTTP/1.1 500 Internal Server Error\r\n\r\nFailed to load plugin");
            }
        }
        return std::string("HTTP/1.1 400 Bad Request\r\n\r\nBad request");
    });

    // Delete plugin
    registerEndpoint("DELETE /plugins/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        std::regex idRegex("/plugins/([^/]+)$");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            // Unload if loaded
            unloadSinglePlugin(id);
            // Delete files
            std::string sourcePath = userPluginsDirectory + "/" + id + ".cpp";
            std::string soPath = userPluginsDirectory + "/" + id + ".so";
            bool removed = false;
            if (std::remove(sourcePath.c_str()) == 0) removed = true;
            if (std::remove(soPath.c_str()) == 0) removed = true;
            if (removed) {
                if (logger) logger->log(LogLevel::Info, "Deleted plugin: " + id);
                return std::string("Plugin deleted successfully\n");
            } else {
                return std::string("HTTP/1.1 404 Not Found\r\n\r\nPlugin not found");
            }
        }
        return std::string("HTTP/1.1 400 Bad Request\r\n\r\nBad request");
    });

    // Upload plugin (.so file)
    registerEndpoint("POST /plugins/upload", [this](const std::string& request) {
        if (logger) logger->log(LogLevel::Debug, "Upload request received, size: " + std::to_string(request.size()));
        
        std::string filename;
        std::string fileData = extractMultipartFile(request, filename);
        
        if (logger) {
            logger->log(LogLevel::Debug, "Extracted filename: " + (filename.empty() ? "<empty>" : filename));
            logger->log(LogLevel::Debug, "Extracted file data size: " + std::to_string(fileData.size()));
        }
        
        if (fileData.empty() || filename.empty()) {
            if (logger) logger->log(LogLevel::Warn, "Upload failed: empty file data or filename");
            return std::string("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 35\r\n\r\n{\"error\":\"No file uploaded\"}");
        }
        
        // Extract moduleId from filename (remove .so extension)
        std::string moduleId = filename;
        if (moduleId.size() > 3 && moduleId.substr(moduleId.size()-3) == ".so") {
            moduleId = moduleId.substr(0, moduleId.size()-3);
        }
        
        // Save to user plugins directory
        std::string destPath = userPluginsDirectory + "/" + filename;
        std::ofstream outFile(destPath, std::ios::binary);
        if (!outFile) {
            if (logger) logger->log(LogLevel::Error, "Failed to open file for writing: " + destPath);
            return std::string("HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: 33\r\n\r\n{\"error\":\"Failed to save file\"}");
        }
        outFile.write(fileData.c_str(), fileData.size());
        outFile.close();
        
        if (logger) logger->log(LogLevel::Info, "Uploaded plugin: " + filename + " (" + std::to_string(fileData.size()) + " bytes)");
        
        std::string responseBody = "{\"success\":true,\"moduleId\":\"" + moduleId + "\"}";
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(responseBody.size()) + "\r\n\r\n" + responseBody;
        return response;
    });

    registerEndpoint("POST /robots/{id}", [this](const std::string& request) {
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

        // Add robot to map if mapId is present
        if (!newRobot.mapId.empty()) {
            auto mIt = maps.find(newRobot.mapId);
            if (mIt != maps.end()) {
                mIt->second->addRobot(newRobot);
            }
        }

        if (logger) logger->log(LogLevel::Info, std::string("Created robot id=") + newRobot.id);
        return std::string("Robot created successfully\n");
    });

    registerEndpoint("POST /robots", [this](const std::string& request) {
        std::string body = extractBody(request);

        std::vector<Robot> newRobots = Robot::deserializeList(body);
        for (const auto& robot : newRobots) {
            robots[robot.id] = robot;
            // Add robot to map if mapId is present
            if (!robot.mapId.empty()) {
                auto mIt = maps.find(robot.mapId);
                if (mIt != maps.end()) {
                    mIt->second->addRobot(robot);
                }
            }
        }

        if (logger) logger->log(LogLevel::Info, "Created " + std::to_string(newRobots.size()) + " robots");
        return std::string("Robots created successfully\n");
    });

    registerEndpoint("PATCH /robots/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            auto it = robots.find(id);
            if (it != robots.end()) {
                // Parse only the fields present in the PATCH body and update selectively
                Robot& existingRobot = it->second;
                
                // Check for position update
                std::regex posRegex("\"position\"\\s*:\\s*\\[\\s*([0-9.+\\-eE]+)\\s*,\\s*([0-9.+\\-eE]+)\\s*\\]");
                std::smatch posMatch;
                if (std::regex_search(body, posMatch, posRegex)) {
                    float x = std::stof(posMatch[1]);
                    float y = std::stof(posMatch[2]);
                    existingRobot.setPosition(x, y);
                    
                    // Update robot in map
                    if (!existingRobot.mapId.empty()) {
                        auto mIt = maps.find(existingRobot.mapId);
                        if (mIt != maps.end()) {
                            Robot* mapRobot = mIt->second->findRobotById(id);
                            if (mapRobot) {
                                mapRobot->setPosition(x, y);
                            }
                        }
                    }

                    if (logger) logger->log(LogLevel::Info, "Updated robot position id=" + id + " to (" + std::to_string(x) + "," + std::to_string(y) + ")");
                }
                
                // Could add more selective field updates here (type, attributes, etc.)
                
                return std::string("Robot updated successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Patch robot not found");
        return std::string("Robot not found\n");
    });

    registerEndpoint("GET /robots", [this](const std::string& request) {
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
        return result;
    });

    registerEndpoint("GET /robots/{id}", [this](const std::string& request) {
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
        return std::string("Robot not found\n");
    });

    registerEndpoint("DELETE /robots/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/robots/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            auto it = robots.find(id);
            if (it != robots.end()) {
                // Remove from map first
                if (!it->second.mapId.empty()) {
                    auto mIt = maps.find(it->second.mapId);
                    if (mIt != maps.end()) {
                        mIt->second->removeRobot(id);
                    }
                }
                
                robots.erase(it);
                if (logger) logger->log(LogLevel::Info, "Deleted robot id=" + id);
                return std::string("Robot deleted successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Delete robot not found");
        return std::string("Robot not found\n");
    });

    registerEndpoint("DELETE /robots", [this](const std::string& request) {
        robots.clear();
        if (logger) logger->log(LogLevel::Info, "Deleted all robots");
        return std::string("All robots deleted successfully\n");
    });

    registerEndpoint("POST /modules", [this](const std::string& request) {
        std::string body = extractBody(request);
        std::vector<Module> newModules = Module::deserializeList(body);
        for (const auto &m : newModules) {
            modules[m.id] = m;
            if (logger) logger->log(LogLevel::Info, "Added module id=" + m.id + " name=" + m.name);
        }
        return std::string("Modules created\n");
    });

    registerEndpoint("POST /modules/{id}", [this](const std::string& request) {
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
        return std::string("Module created\n");
    });

    registerEndpoint("GET /modules", [this](const std::string& request) {
        std::ostringstream out;
        out << "[";
        for (const auto &kv : modules) {
            out << kv.second.serialize() << ",";
        }
        std::string s = out.str();
        if (!modules.empty()) s.pop_back();
        s += "]";
        if (logger) logger->log(LogLevel::Info, "Fetched all modules, count=" + std::to_string(modules.size()));
        return s;
    });

    registerEndpoint("GET /modules/{id}", [this](const std::string& request) {
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
        return std::string("Module not found\n");
    });

    registerEndpoint("PATCH /modules/{id}", [this](const std::string& request) {
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
        return std::string("Module not found\n");
    });

    registerEndpoint("DELETE /modules/{id}", [this](const std::string& request) {
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
        return std::string("Module not found\n");
    });


    registerEndpoint("POST /map/{id}", [this](const std::string& request) {
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
            std::regex nameRegex("\"name\"\\s*:\\s*\"([^\"]+)\"");
            std::regex mapUrlRegex("\"mapUrl\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch widthMatch, heightMatch, nameMatch, mapUrlMatch;

            if (std::regex_search(body, widthMatch, widthRegex) && 
                std::regex_search(body, heightMatch, heightRegex) &&
                std::regex_search(body, nameMatch, nameRegex) &&
                std::regex_search(body, mapUrlMatch, mapUrlRegex)) {
                
                int width = std::stoi(widthMatch[1]);
                int height = std::stoi(heightMatch[1]);
                std::string name = nameMatch[1];
                std::string mapUrl = mapUrlMatch[1];
                
                auto mapResult = maps.emplace(id, std::make_unique<Map>(width, height, name, mapUrl));

                // Create a TaskManager for this map
                taskManagers[id] = std::make_unique<TaskManager>(*(mapResult.first->second));

                if (logger) logger->log(LogLevel::Info, "Created map with id=" + id + ", name=" + name + ", width=" + std::to_string(width) + ", height=" + std::to_string(height) + ", mapUrl=" + mapUrl);

                // If the mapUrl appears to be a local image file, attempt to run
                // the segmentation script to populate the Map cells before routing.
                try {
                    // naive check for image extension
                    std::string lowerUrl = mapUrl;
                    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
                    bool isImage = (lowerUrl.size() > 4 && (lowerUrl.substr(lowerUrl.size()-4) == ".jpg" || lowerUrl.substr(lowerUrl.size()-4) == ".png" || lowerUrl.substr(lowerUrl.size()-5) == ".jpeg"));
                    if (isImage) {
                        // Prepare local image path: if mapUrl is remote, download it first
                        std::string localImgPath = mapUrl;
                        bool downloaded = false;
                        if (lowerUrl.rfind("http://", 0) == 0 || lowerUrl.rfind("https://", 0) == 0) {
                            localImgPath = "/tmp/seg_img_" + id + ".img";
                            const char* token = std::getenv("MAPBOX_ACCESS_TOKEN");
                            std::ostringstream dl;
                            dl << "curl -s -L -o \"" << localImgPath << "\" \"" << mapUrl << "\"";
                            if (token && token[0] != '\0') {
                                dl << " -H \"Authorization: Bearer " << token << "\"";
                            }
                            std::string dlcmd = dl.str();
                            if (logger) logger->log(LogLevel::Info, "Downloading map image: " + dlcmd);
                            int drc = std::system(dlcmd.c_str());
                            if (drc != 0) {
                                if (logger) logger->log(LogLevel::Warn, "Failed to download map image, curl rc=" + std::to_string(drc));
                            } else {
                                downloaded = true;
                            }
                        }

                        // Check file exists (either original local path or downloaded)
                        std::ifstream imgFile(localImgPath);
                        if (imgFile.good()) {
                            imgFile.close();
                            std::string tmpJson = "/tmp/seg_map_" + id + ".json";
                            // Build command to run the Python script. Use repo-relative path.
                            std::ostringstream cmd;
                            cmd << "python3 scripts/segment_and_export.py \"" << localImgPath << "\" \"/tmp/seg_out_" << id << ".hpp\" --format map_class --out-json \"" << tmpJson << "\" --grid " << std::to_string(width);
                            std::string command = cmd.str();
                            if (logger) logger->log(LogLevel::Info, "Running segmentation command: " + command);
                            int rc = std::system(command.c_str());
                            if (rc == 0) {
                                // Read JSON and populate the map grid
                                std::ifstream jf(tmpJson);
                                if (jf) {
                                    std::string content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
                                    jf.close();
                                    // Very small JSON parser: extract numbers sequentially
                                    std::vector<int> nums;
                                    std::string num;
                                    for (size_t i = 0; i < content.size(); ++i) {
                                        char c = content[i];
                                        if ((c >= '0' && c <= '9')) {
                                            num.push_back(c);
                                        } else if (!num.empty()) {
                                            nums.push_back(std::stoi(num));
                                            num.clear();
                                        }
                                    }
                                    if (!num.empty()) { nums.push_back(std::stoi(num)); num.clear(); }
                                    // Expect at least width and height as first two numbers followed by grid entries
                                    if (nums.size() >= 2) {
                                        int jwidth = nums[0];
                                        int jheight = nums[1];
                                        size_t expect = 2 + (size_t)jwidth * (size_t)jheight;
                                        if (nums.size() >= expect) {
                                            Map &mref = *maps.at(id);
                                            size_t idx = 2;
                                            for (int y = 0; y < jheight; ++y) {
                                                for (int x = 0; x < jwidth; ++x) {
                                                    int code = nums[idx++];
                                                    // code: 1=FIELD,2=ROAD => accessible(0); else inaccessible(1)
                                                    int cell = (code == 1 || code == 2) ? 0 : 1;
                                                    // If generated grid resolution differs from Map width/height, scale accordingly
                                                    if (jwidth == mref.getWidth() && jheight == mref.getHeight()) {
                                                        mref.setCell(x, y, cell);
                                                    } else {
                                                        // Map and generated grid sizes differ: map cells are indexed by map's width/height
                                                        int scaled_x = x * mref.getWidth() / jwidth;
                                                        int scaled_y = y * mref.getHeight() / jheight;
                                                        if (mref.isValidPosition(scaled_x, scaled_y)) {
                                                            mref.setCell(scaled_x, scaled_y, cell);
                                                        }
                                                    }
                                                }
                                            }
                                            if (logger) logger->log(LogLevel::Info, "Populated map grid from segmentation for map id=" + id);
                                        } else {
                                            if (logger) logger->log(LogLevel::Warn, "Segmentation JSON smaller than expected for map id=" + id);
                                        }
                                    }
                                } else {
                                    if (logger) logger->log(LogLevel::Warn, "Failed to open segmentation JSON: " + tmpJson);
                                }
                                // cleanup tmp files
                                std::remove(tmpJson.c_str());
                                if (downloaded) std::remove(localImgPath.c_str());
                            } else {
                                if (logger) logger->log(LogLevel::Warn, "Segmentation script failed with rc=" + std::to_string(rc));
                                if (downloaded) std::remove(localImgPath.c_str());
                            }
                        } else {
                            if (logger) logger->log(LogLevel::Warn, "Map image file not found: " + localImgPath);
                        }
                    }
                } catch (const std::exception &ex) {
                    if (logger) logger->log(LogLevel::Error, std::string("Exception during segmentation: ") + ex.what());
                }

                return std::string("Map created successfully\n");
            } else {
                return std::string("Failed to parse map data (missing required fields: width, height, name, mapUrl)\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Failed to create map (bad path)");
        return std::string("Failed to create map\n"); });

    registerEndpoint("PATCH /map/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (maps.find(id) != maps.end()) {
                // TODO: Implement Map::deserialize or parse JSON body
                // maps[id] = Map::deserialize(body);
                if (logger) logger->log(LogLevel::Info, std::string("Updated map id=") + id);
                return std::string("Map updated successfully\n");
            }
        }

        return std::string("Map not found\n");
    });

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
                const Map &m = *(it->second);
                std::ostringstream out;
                out << "{\"id\":\"" << id << "\",\"name\":\"" << m.getName() << "\""
                    << ",\"width\":" << m.getWidth() << ",\"height\":" << m.getHeight() 
                    << ",\"mapUrl\":\"" << m.getMapUrl() << "\"}";
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
                    << ",\"name\":\"" << map->getName() << "\""
                    << ",\"width\":" << map->getWidth() 
                    << ",\"height\":" << map->getHeight() 
                    << ",\"mapUrl\":\"" << map->getMapUrl() << "\""
                    << "},";
        }
        std::string result = response.str();
        if (!maps.empty()) {
            result.pop_back(); // Remove trailing comma
        }
        result += "]";

        return result; });

    registerEndpoint("DELETE /map/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (maps.erase(id)) {
                // Delete TaskManager for this map
                taskManagers.erase(id);

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
        return std::string("Map not found\n");
    });

    // Endpoint to invoke pathfinding for a robot against a specific map
    // Expects JSON body: {"mapId":"<map-uuid>","target":[x,y]}
    registerEndpoint("POST /robots/{id}/pathfind", [this](const std::string& request) {
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

            // If the map was created from an image but segmentation hasn't run
            // (grid likely all zeros), attempt to run segmentation now to populate
            // obstacles before pathfinding.
            try {
                Map &mref = *mIt->second;
                bool allZero = true;
                for (int yy = 0; yy < mref.getHeight() && allZero; ++yy) {
                    for (int xx = 0; xx < mref.getWidth(); ++xx) {
                        if (mref.getCell(xx, yy) != 0) { allZero = false; break; }
                    }
                }
                std::string mapUrlLocal = mref.getMapUrl();
                std::string lowerUrl = mapUrlLocal;
                std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
                bool isImage = (lowerUrl.size() > 4 && (lowerUrl.substr(lowerUrl.size()-4) == ".jpg" || lowerUrl.substr(lowerUrl.size()-4) == ".png" || lowerUrl.substr(lowerUrl.size()-5) == ".jpeg"));
                if (allZero && isImage) {
                    // Prepare local image path: download if remote
                    std::string localImgPath = mapUrlLocal;
                    bool downloaded = false;
                    if (lowerUrl.rfind("http://", 0) == 0 || lowerUrl.rfind("https://", 0) == 0) {
                        localImgPath = "/tmp/seg_img_" + mapId + ".img";
                        const char* token = std::getenv("MAPBOX_ACCESS_TOKEN");
                        std::ostringstream dl;
                        dl << "curl -s -L -o \"" << localImgPath << "\" \"" << mapUrlLocal << "\"";
                        if (token && token[0] != '\0') {
                            dl << " -H \"Authorization: Bearer " << token << "\"";
                        }
                        std::string dlcmd = dl.str();
                        if (logger) logger->log(LogLevel::Info, "Downloading map image before pathfind: " + dlcmd);
                        int drc = std::system(dlcmd.c_str());
                        if (drc != 0) {
                            if (logger) logger->log(LogLevel::Warn, "Failed to download map image before pathfind, curl rc=" + std::to_string(drc));
                        } else {
                            downloaded = true;
                        }
                    }

                    std::ifstream imgFile(localImgPath);
                    if (imgFile.good()) {
                        imgFile.close();
                        std::string tmpJson = "/tmp/seg_map_" + mapId + ".json";
                        std::ostringstream cmd;
                        cmd << "python3 scripts/segment_and_export.py \"" << localImgPath << "\" \"/tmp/seg_out_" << mapId << ".hpp\" --format map_class --out-json \"" << tmpJson << "\" --grid " << std::to_string(mref.getWidth());
                        std::string command = cmd.str();
                        if (logger) logger->log(LogLevel::Info, "Running segmentation before pathfind: " + command);
                        int rc = std::system(command.c_str());
                        if (rc == 0) {
                            std::ifstream jf(tmpJson);
                            if (jf) {
                                std::string content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
                                jf.close();
                                std::vector<int> nums;
                                std::string num;
                                for (size_t i = 0; i < content.size(); ++i) {
                                    char c = content[i];
                                    if ((c >= '0' && c <= '9')) {
                                        num.push_back(c);
                                    } else if (!num.empty()) {
                                        nums.push_back(std::stoi(num));
                                        num.clear();
                                    }
                                }
                                if (!num.empty()) { nums.push_back(std::stoi(num)); num.clear(); }
                                if (nums.size() >= 2) {
                                    int jwidth = nums[0];
                                    int jheight = nums[1];
                                    size_t expect = 2 + (size_t)jwidth * (size_t)jheight;
                                    if (nums.size() >= expect) {
                                        size_t idx = 2;
                                        for (int y = 0; y < jheight; ++y) {
                                            for (int x = 0; x < jwidth; ++x) {
                                                int code = nums[idx++];
                                                int cell = (code == 1 || code == 2) ? 0 : 1;
                                                if (jwidth == mref.getWidth() && jheight == mref.getHeight()) {
                                                    mref.setCell(x, y, cell);
                                                } else {
                                                    int scaled_x = x * mref.getWidth() / jwidth;
                                                    int scaled_y = y * mref.getHeight() / jheight;
                                                    if (mref.isValidPosition(scaled_x, scaled_y)) {
                                                        mref.setCell(scaled_x, scaled_y, cell);
                                                    }
                                                }
                                            }
                                        }
                                        if (logger) logger->log(LogLevel::Info, "Populated map grid from segmentation before pathfind for map id=" + mapId);
                                    }
                                }
                            }
                            std::remove(tmpJson.c_str());
                            if (downloaded) std::remove(localImgPath.c_str());
                        } else {
                            if (logger) logger->log(LogLevel::Warn, "Segmentation script failed before pathfind with rc=" + std::to_string(rc));
                            if (downloaded) std::remove(localImgPath.c_str());
                        }
                    }
                }
            } catch (...) {}

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

            // Clear simulation log before starting new pathfinding
            std::remove("simulation.log");

            // Execute pathfinding (this will append to simulation.log)
            try {
                rIt->second.pathfind(*(mIt->second), std::vector<float>{tx, ty});
            } catch (const std::exception& ex) {
                if (logger) logger->log(LogLevel::Error, std::string("Pathfind exception: ") + ex.what());
                return std::string("Pathfind failed\n");
            }

            if (logger) logger->log(LogLevel::Info, "Pathfind executed for robot=" + robotId + " map=" + mapId);
            return std::string("Pathfind executed\n");
        }

        return std::string("Bad request\n");
    });

    // GET /simulation/events - Read and parse simulation.log
    registerEndpoint("GET /simulation/events", [this](const std::string& request) {
        std::ifstream logFile("simulation.log");
        if (!logFile.is_open()) {
            if (logger) logger->log(LogLevel::Warn, "simulation.log not found");
            return std::string("{\"events\":[]}\n");
        }

        std::ostringstream out;
        out << "{\"events\":[";
        std::string line;
        bool first = true;

        while (std::getline(logFile, line)) {
            if (line.empty()) continue;

            if (!first) out << ",";
            first = false;

            // Parse: "TIMESTAMP EVENT_TYPE data..."
            // Timestamp is first 23 chars: "YYYY-MM-DD HH:MM:SS.mmm"
            if (line.size() < 24) continue;

            std::string timestamp = line.substr(0, 23);
            size_t typeStart = 24;
            size_t spaceAfterType = line.find(' ', typeStart);
            
            std::string eventType;
            std::string data;
            
            if (spaceAfterType == std::string::npos) {
                eventType = line.substr(typeStart);
                data = "";
            } else {
                eventType = line.substr(typeStart, spaceAfterType - typeStart);
                data = line.substr(spaceAfterType + 1);
            }

            // Escape quotes in data for JSON
            std::string escapedData;
            for (char c : data) {
                if (c == '"') escapedData += "\\\"";
                else if (c == '\\') escapedData += "\\\\";
                else escapedData += c;
            }

            out << "{";
            out << "\"timestamp\":\"" << timestamp << "\",";
            out << "\"type\":\"" << eventType << "\",";
            out << "\"data\":\"" << escapedData << "\"";
            out << "}";
        }
        out << "]}";
        logFile.close();

        if (logger) logger->log(LogLevel::Info, "Served simulation events");
        return out.str();
    });

    // POST /simulation/clear - Clear simulation.log
    registerEndpoint("POST /simulation/clear", [this](const std::string& request) {
        std::ofstream logFile("simulation.log", std::ofstream::out | std::ofstream::trunc);
        if (!logFile.is_open()) {
             if (logger) logger->log(LogLevel::Warn, "Failed to clear simulation.log");
             return std::string("{\"error\":\"Failed to clear log\"}\n");
        }
        logFile.close();
        if (logger) logger->log(LogLevel::Info, "Cleared simulation events");
        return std::string("{\"success\":true}\n");
    });

    // ===== TASK MANAGEMENT ENDPOINTS =====

    // POST /tasks - Create a new task
    registerEndpoint("POST /tasks", [this](const std::string& request) {
        std::string body = extractBody(request);

        // Parse JSON: {mapId, targetPosition:[x,y], priority, description, moduleIds:["id1","id2"]}
        std::regex mapIdRe("\"mapId\"\\s*:\\s*\"([^\"]+)\"");
        std::regex targetRe("\"targetPosition\"\\s*:\\s*\\[\\s*([0-9.+\\-eE]+)\\s*,\\s*([0-9.+\\-eE]+)\\s*\\]");
        std::regex priorityRe("\"priority\"\\s*:\\s*([0-9]+)");
        std::regex descRe("\"description\"\\s*:\\s*\"([^\"]+)\"");

        std::smatch m1, m2, m3, m4;
        if (!std::regex_search(body, m1, mapIdRe) || !std::regex_search(body, m2, targetRe)) {
            return std::string("{\"error\":\"Missing mapId or targetPosition\"}\n");
        }

        std::string mapId = m1[1];
        float x = std::stof(m2[1]);
        float y = std::stof(m2[2]);
        int priority = std::regex_search(body, m3, priorityRe) ? std::stoi(m3[1]) : 0;
        std::string description = std::regex_search(body, m4, descRe) ? m4[1].str() : "";

        // Parse moduleIds array: ["id1","id2",...]
        std::vector<std::string> moduleIds;
        std::regex moduleIdsRe("\"moduleIds\"\\s*:\\s*\\[([^\\]]+)\\]");
        std::smatch m5;
        if (std::regex_search(body, m5, moduleIdsRe)) {
            std::string moduleIdsStr = m5[1];
            std::regex moduleRe("\"([^\"]+)\"");
            std::sregex_iterator iter(moduleIdsStr.begin(), moduleIdsStr.end(), moduleRe);
            std::sregex_iterator end;
            while (iter != end) {
                moduleIds.push_back((*iter)[1].str());
                ++iter;
            }
        }

        auto tmIt = taskManagers.find(mapId);
        if (tmIt == taskManagers.end()) {
            return std::string("{\"error\":\"Map not found\"}\n");
        }

        // Create task using TaskManager's auto-ID generation
        // Don't use addTask(Task&) - it expects task.id to be set
        // Instead, manually create and add with proper ID generation
        Task task;
        task.id = tmIt->second->generateTaskId();  // Generate unique ID
        task.targetPosition = {x, y};
        task.priority = priority;
        task.description = description;
        task.moduleIds = moduleIds;
        tmIt->second->addTask(task);

        if (logger) logger->log(LogLevel::Info, "Created task for map=" + mapId + " with " + std::to_string(moduleIds.size()) + " modules");
        return std::string("{\"success\":true}\n");
    });

    // GET /tasks?mapId={id} - List all tasks for a map
    registerEndpoint("GET /tasks", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex mapIdRe("mapId=([^&\\s]+)");
        std::smatch match;
        if (!std::regex_search(path, match, mapIdRe)) {
            return std::string("{\"error\":\"Missing mapId parameter\"}\n");
        }

        std::string mapId = match[1];
        auto tmIt = taskManagers.find(mapId);
        if (tmIt == taskManagers.end()) {
            return std::string("{\"error\":\"Map not found\"}\n");
        }

        auto tasks = tmIt->second->getPendingTasks();
        std::ostringstream out;
        out << "{\"tasks\":[";
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (i > 0) out << ",";
            out << "{";
            out << "\"id\":\"" << tasks[i].id << "\",";
            out << "\"description\":\"" << tasks[i].description << "\",";
            out << "\"targetPosition\":[" << tasks[i].targetPosition[0] << "," << tasks[i].targetPosition[1] << "],";
            out << "\"priority\":" << tasks[i].priority << ",";
            out << "\"moduleIds\":[";
            for (size_t j = 0; j < tasks[i].moduleIds.size(); ++j) {
                if (j > 0) out << ",";
                out << "\"" << tasks[i].moduleIds[j] << "\"";
            }
            out << "]";
            out << "}";
        }
        out << "]}";
        return out.str();
    });

    // POST /tasks/assign?mapId={id}&algorithm={greedy|optimal|balanced} - Assign tasks to robots
    registerEndpoint("POST /tasks/assign", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex mapIdRe("mapId=([^&\\s]+)");
        std::regex algoRe("algorithm=([^&\\s]+)");
        std::smatch m1, m2;

        if (!std::regex_search(path, m1, mapIdRe)) {
            return std::string("{\"error\":\"Missing mapId parameter\"}\n");
        }

        std::string mapId = m1[1];
        std::string algorithm = std::regex_search(path, m2, algoRe) ? m2[1].str() : "greedy";

        auto tmIt = taskManagers.find(mapId);
        if (tmIt == taskManagers.end()) {
            return std::string("{\"error\":\"Map not found\"}\n");
        }

        // Clear simulation log before starting new multi-robot simulation
        std::remove("simulation.log");

        // Clear previous task assignments to make all robots available
        tmIt->second->clearAllAssignments();

        // Log task and robot counts
        auto pendingTasks = tmIt->second->getPendingTasks();
        auto& robots = maps[mapId]->getRobots();
        if (logger) {
            logger->log(LogLevel::Info, "Starting task assignment: " + std::to_string(pendingTasks.size()) + " tasks, " + std::to_string(robots.size()) + " robots on map");
        }

        std::ostringstream out;
        out << "{\"assignments\":[";

        if (algorithm == "optimal") {
            auto assignments = tmIt->second->assignAllTasksOptimal();
            if (logger) logger->log(LogLevel::Info, "Optimal algorithm assigned " + std::to_string(assignments.size()) + " robots to tasks");
            int idx = 0;
            for (const auto& [taskId, robotId] : assignments) {
                if (idx++ > 0) out << ",";
                out << "{\"taskId\":\"" << taskId << "\",\"robotId\":\"" << robotId << "\"}";
            }
            out << "]}";
        } else if (algorithm == "balanced") {
            auto assignments = tmIt->second->assignAllTasksBalanced();
            if (logger) logger->log(LogLevel::Info, "Balanced algorithm assigned " + std::to_string(assignments.size()) + " robots to tasks");
            int idx = 0;
            for (const auto& [taskId, robotId] : assignments) {
                if (idx++ > 0) out << ",";
                out << "{\"taskId\":\"" << taskId << "\",\"robotId\":\"" << robotId << "\"}";
            }
            out << "]}";
        } else { // greedy (default)
            int assignmentCount = 0;
            while (true) {
                auto robotId = tmIt->second->assignNextTaskNearestRobot();
                if (!robotId.has_value()) break;
                assignmentCount++;
            }
            if (logger) logger->log(LogLevel::Info, "Greedy algorithm assigned " + std::to_string(assignmentCount) + " robots to tasks");
            out << "]}";
        }

        return out.str();
    });

    // GET /tasks/assignments?mapId={id} - Get current task assignments
    registerEndpoint("GET /tasks/assignments", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex mapIdRe("mapId=([^&\\s]+)");
        std::smatch match;
        if (!std::regex_search(path, match, mapIdRe)) {
            return std::string("{\"error\":\"Missing mapId parameter\"}\n");
        }

        std::string mapId = match[1];
        auto tmIt = taskManagers.find(mapId);
        if (tmIt == taskManagers.end()) {
            return std::string("{\"error\":\"Map not found\"}\n");
        }

        const auto& assignments = tmIt->second->getAssignments();
        std::ostringstream out;
        out << "{\"assignments\":[";
        int idx = 0;
        for (const auto& [taskId, robotId] : assignments) {
            if (idx++ > 0) out << ",";
            out << "{\"taskId\":\"" << taskId << "\",\"robotId\":\"" << robotId << "\"}";
        }
        out << "]}";
        return out.str();
    });
}

void Server::start() {
    initializeHandlers();
    running = true;
    serverThread = std::thread(&Server::run, this);
    if (logger) logger->log(LogLevel::Info, "Server started on port " + std::to_string(port));
}

void Server::stop() {
    running = false;
    if (serverThread.joinable()) {
        serverThread.join();
    }
    // unload any plugins that were loaded
    unloadPlugins();

    if (logger) logger->log(LogLevel::Info, "Server stopped.");
}

// Load all .so files in dirPath. For each plugin, call plugin_start(&hostApi, moduleId)
int Server::loadPluginsFromDirectory(const std::string& dirPath) {
    // remember directory for listing
    pluginsDirectory = dirPath;

    int loaded = 0;

    // Helper function to load plugins from a directory (non-recursive in this call)
    auto loadFromDir = [&](const std::string& path) -> int {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            if (logger) logger->log(LogLevel::Warn, std::string("Failed to open plugins directory: ") + path);
            return 0;
        }

        int count = 0;
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            if (name.size() > 3 && name.substr(name.size()-3) == ".so") {
                std::string fullpath = path + "/" + name;
                void* handle = dlopen(fullpath.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!handle) {
                    if (logger) logger->log(LogLevel::Error, std::string("dlopen failed for ") + fullpath + ": " + dlerror());
                    continue;
                }

                using start_fn_t = int(*)(const HostAPI*, const char*);
                using stop_fn_t = void(*)();

                dlerror(); // clear
                start_fn_t start = reinterpret_cast<start_fn_t>(dlsym(handle, "plugin_start"));
                const char* dlsym_err = dlerror();
                if (dlsym_err || !start) {
                    if (logger) logger->log(LogLevel::Warn, std::string("plugin_start not found in ") + fullpath);
                    dlclose(handle);
                    continue;
                }

                stop_fn_t stop = reinterpret_cast<stop_fn_t>(dlsym(handle, "plugin_stop"));
                // stop may be optional; still allow plugin to load

                // Use file base name (without .so) as moduleId
                std::string moduleId = name.substr(0, name.size()-3);

                int rc = start(&hostApi, moduleId.c_str());
                if (rc != 0) {
                    if (logger) logger->log(LogLevel::Warn, std::string("plugin_start failed for ") + fullpath);
                    if (stop) stop();
                    dlclose(handle);
                    continue;
                }

                PluginEntry entry;
                entry.handle = handle;
                entry.stopFn = stop;
                entry.path = fullpath;
                entry.moduleId = moduleId;
                loadedPlugins.push_back(entry);
                ++count;
                if (logger) logger->log(LogLevel::Info, std::string("Loaded plugin: ") + fullpath + " as moduleId=" + moduleId);
            }
        }

        closedir(dir);
        return count;
    };

    // Load from the specified directory
    loaded += loadFromDir(dirPath);

    // Also scan subdirectories recursively (one level deep for now)
    DIR* dir = opendir(dirPath.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;

            std::string subpath = dirPath + "/" + name;
            DIR* subdir = opendir(subpath.c_str());
            if (subdir) {
                closedir(subdir);
                // It's a directory, try loading plugins from it
                if (logger) logger->log(LogLevel::Info, std::string("Scanning subdirectory: ") + subpath);
                loaded += loadFromDir(subpath);
            }
        }
        closedir(dir);
    }

    if (logger) logger->log(LogLevel::Info, std::string("Total plugins loaded: ") + std::to_string(loaded));
    return loaded;
}

// After initializeHandlers registration, expose endpoints to get/set enabled plugins
    

void Server::unloadPlugins() {
    // Unregister and dlclose in reverse order
    for (auto it = loadedPlugins.rbegin(); it != loadedPlugins.rend(); ++it) {
        if (it->stopFn) {
            try { it->stopFn(); } catch (...) {}
        }
        if (it->handle) {
            dlclose(it->handle);
            it->handle = nullptr;
        }
        if (logger) logger->log(LogLevel::Info, std::string("Unloaded plugin: ") + it->path);
    }
    loadedPlugins.clear();
}

std::string Server::readPluginSource(const std::string& moduleId) {
    std::string sourcePath = userPluginsDirectory + "/" + moduleId + ".cpp";
    std::ifstream inFile(sourcePath);
    if (!inFile) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    return content;
}

bool Server::savePluginSource(const std::string& moduleId, const std::string& source) {
    std::string sourcePath = userPluginsDirectory + "/" + moduleId + ".cpp";
    std::ofstream outFile(sourcePath);
    if (!outFile) {
        return false;
    }
    outFile << source;
    outFile.close();
    return true;
}

std::string Server::compilePlugin(const std::string& moduleId, const std::string& sourceCode) {
    // Save source code first
    if (!savePluginSource(moduleId, sourceCode)) {
        return "{\"success\":false,\"output\":\"\",\"errors\":\"Failed to save source file\"}";
    }
    
    std::string sourcePath = userPluginsDirectory + "/" + moduleId + ".cpp";
    std::string outputPath = userPluginsDirectory + "/" + moduleId + ".so";
    
    // Build compilation command
    std::ostringstream cmd;
    cmd << "g++ -I" << pluginsDirectory << "/.. "
        << "-fPIC -Wall -O2 -std=c++17 -shared "
        << "-o " << outputPath << " "
        << sourcePath << " 2>&1";
    
    std::string command = cmd.str();
    
    if (logger) logger->log(LogLevel::Info, "Compiling plugin: " + command);
    
    // Execute compilation
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "{\"success\":false,\"output\":\"\",\"errors\":\"Failed to execute compiler\"}";
    }
    
    std::string compileOutput;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        compileOutput += buffer;
    }
    
    int returnCode = pclose(pipe);
    
    // Escape JSON special characters
    std::string escapedOutput;
    for (char c : compileOutput) {
        if (c == '"') escapedOutput += "\\\"";
        else if (c == '\\') escapedOutput += "\\\\";
        else if (c == '\n') escapedOutput += "\\n";
        else if (c == '\r') escapedOutput += "\\r";
        else if (c == '\t') escapedOutput += "\\t";
        else escapedOutput += c;
    }
    
    std::ostringstream result;
    result << "{\"success\":" << (returnCode == 0 ? "true" : "false")
           << ",\"output\":\"" << escapedOutput << "\""
           << ",\"errors\":\"" << (returnCode != 0 ? escapedOutput : "") << "\"}";
    
    return result.str();
}

bool Server::hotLoadPlugin(const std::string& moduleId) {
    // First, unload if already loaded
    unloadSinglePlugin(moduleId);
    
    std::string pluginPath = userPluginsDirectory + "/" + moduleId + ".so";
    
    // Check if file exists
    std::ifstream checkFile(pluginPath);
    if (!checkFile.good()) {
        if (logger) logger->log(LogLevel::Error, "Plugin file not found: " + pluginPath);
        return false;
    }
    checkFile.close();
    
    // Load plugin using dlopen
    void* handle = dlopen(pluginPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        if (logger) logger->log(LogLevel::Error, std::string("dlopen failed for ") + pluginPath + ": " + dlerror());
        return false;
    }
    
    using start_fn_t = int(*)(const HostAPI*, const char*);
    using stop_fn_t = void(*)();
    
    dlerror(); // clear
    start_fn_t start = reinterpret_cast<start_fn_t>(dlsym(handle, "plugin_start"));
    const char* dlsym_err = dlerror();
    if (dlsym_err || !start) {
        if (logger) logger->log(LogLevel::Warn, std::string("plugin_start not found in ") + pluginPath);
        dlclose(handle);
        return false;
    }
    
    stop_fn_t stop = reinterpret_cast<stop_fn_t>(dlsym(handle, "plugin_stop"));
    
    int rc = start(&hostApi, moduleId.c_str());
    if (rc != 0) {
        if (logger) logger->log(LogLevel::Warn, std::string("plugin_start failed for ") + pluginPath);
        if (stop) stop();
        dlclose(handle);
        return false;
    }
    
    PluginEntry entry;
    entry.handle = handle;
    entry.stopFn = stop;
    entry.path = pluginPath;
    entry.moduleId = moduleId;
    loadedPlugins.push_back(entry);
    
    if (logger) logger->log(LogLevel::Info, std::string("Hot-loaded plugin: ") + pluginPath);
    return true;
}

bool Server::unloadSinglePlugin(const std::string& moduleId) {
    auto it = std::find_if(loadedPlugins.begin(), loadedPlugins.end(),
                          [&moduleId](const PluginEntry& entry) {
                              return entry.moduleId == moduleId;
                          });
    
    if (it != loadedPlugins.end()) {
        if (it->stopFn) {
            try { it->stopFn(); } catch (...) {}
        }
        if (it->handle) {
            dlclose(it->handle);
            it->handle = nullptr;
        }
        if (logger) logger->log(LogLevel::Info, std::string("Unloaded plugin: ") + it->path);
        loadedPlugins.erase(it);
        return true;
    }
    return false;
}

std::string Server::extractMultipartFile(const std::string& request, std::string& filename) {
    // Parse multipart/form-data
    // Find boundary in Content-Type header
    size_t contentTypePos = request.find("Content-Type:");
    if (contentTypePos == std::string::npos) {
        return "";
    }
    
    size_t boundaryPos = request.find("boundary=", contentTypePos);
    if (boundaryPos == std::string::npos) {
        return "";
    }
    
    boundaryPos += 9; // skip "boundary="
    // Skip any whitespace or quotes
    while (boundaryPos < request.size() && (request[boundaryPos] == ' ' || request[boundaryPos] == '"')) {
        boundaryPos++;
    }
    
    size_t boundaryEnd = boundaryPos;
    while (boundaryEnd < request.size() && 
           request[boundaryEnd] != '\r' && 
           request[boundaryEnd] != '\n' && 
           request[boundaryEnd] != ';' &&
           request[boundaryEnd] != '"') {
        boundaryEnd++;
    }
    
    std::string boundaryValue = request.substr(boundaryPos, boundaryEnd - boundaryPos);
    std::string boundary = "--" + boundaryValue;
    
    // Find filename
    size_t filenamePos = request.find("filename=\"");
    if (filenamePos == std::string::npos) {
        // Try without quotes
        filenamePos = request.find("filename=");
        if (filenamePos != std::string::npos) {
            filenamePos += 9; // skip "filename="
            size_t filenameEnd = filenamePos;
            while (filenameEnd < request.size() && 
                   request[filenameEnd] != '\r' && 
                   request[filenameEnd] != '\n' &&
                   request[filenameEnd] != ';') {
                filenameEnd++;
            }
            filename = request.substr(filenamePos, filenameEnd - filenamePos);
            // Remove quotes if present
            if (filename.size() >= 2 && filename[0] == '"' && filename[filename.size()-1] == '"') {
                filename = filename.substr(1, filename.size() - 2);
            }
        } else {
            return "";
        }
    } else {
        filenamePos += 10; // skip 'filename="'
        size_t filenameEnd = request.find("\"", filenamePos);
        if (filenameEnd == std::string::npos) {
            return "";
        }
        filename = request.substr(filenamePos, filenameEnd - filenamePos);
    }
    
    // Find file data (after \r\n\r\n following the headers)
    size_t dataStart = request.find("\r\n\r\n");
    if (dataStart == std::string::npos) {
        dataStart = request.find("\n\n");
        if (dataStart == std::string::npos) {
            return "";
        }
        dataStart += 2;
    } else {
        dataStart += 4;
    }
    
    // Find end boundary - search from the end backwards to avoid false matches in binary data
    // First, try to find the closing boundary (with -- at end, which marks the end of multipart)
    size_t dataEnd = request.find(boundary + "--", dataStart);
    if (dataEnd == std::string::npos) {
        // If no closing boundary, look for the next part boundary
        // Search from near the end to avoid false matches in binary data
        size_t searchStart = dataStart;
        size_t searchEnd = request.size();
        
        // For binary files, search backwards from the end
        // But we need to be careful - search for boundary preceded by \r\n
        std::string boundaryMarker = "\r\n" + boundary;
        dataEnd = request.find(boundaryMarker, searchStart);
        if (dataEnd == std::string::npos) {
            // Try with just \n
            boundaryMarker = "\n" + boundary;
            dataEnd = request.find(boundaryMarker, searchStart);
        }
        
        if (dataEnd == std::string::npos) {
            // Last resort: search for boundary at start of line
            // This is less reliable for binary data but might work
            dataEnd = request.find(boundary, searchStart);
            if (dataEnd != std::string::npos) {
                // Verify it's at start of line (preceded by \r\n or \n)
                if (dataEnd > 0 && request[dataEnd-1] != '\n' && request[dataEnd-1] != '\r') {
                    // False match, search again from after this position
                    dataEnd = request.find(boundary, dataEnd + 1);
                }
            }
        } else {
            // Found boundary marker, adjust position
            dataEnd += (boundaryMarker[0] == '\r' ? 2 : 1);
        }
        
        if (dataEnd == std::string::npos) {
            // If still not found, the request might be incomplete or malformed
            // Return empty to indicate failure
            return "";
        }
    }
    
    // Back up to remove trailing \r\n before boundary
    if (dataEnd >= 2 && request[dataEnd-2] == '\r' && request[dataEnd-1] == '\n') {
        dataEnd -= 2;
    } else if (dataEnd >= 1 && request[dataEnd-1] == '\n') {
        dataEnd -= 1;
    }
    
    // Ensure we don't have a negative size
    if (dataEnd <= dataStart) {
        return "";
    }
    
    return request.substr(dataStart, dataEnd - dataStart);
}

void Server::registerEndpoint(const std::string& endpoint, std::function<std::string(const std::string&)> handler) {
    endpointHandlers[endpoint] = handler;
}

void Server::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, 3) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    while (running) {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd < 0) {
            if (running && logger) {
                logger->log(LogLevel::Error, "Failed to accept connection");
            }
            continue;
        }

        if (logger) logger->log(LogLevel::Info, std::string("New client accepted from ") + inet_ntoa(client_address.sin_addr) + ":" + std::to_string(ntohs(client_address.sin_port)));

        // Read the full request (handle large payloads)
        std::string request;
        char buffer[4096] = {0};
        int bytes_read;
        int contentLength = -1;
        size_t bodyStart = 0;
        
        while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
            request.append(buffer, bytes_read);
            
            // Check if we've read the headers
            if (bodyStart == 0) {
                bodyStart = request.find("\r\n\r\n");
                if (bodyStart == std::string::npos) {
                    bodyStart = request.find("\n\n");
                    if (bodyStart != std::string::npos) {
                        bodyStart += 2;
                    }
                } else {
                    bodyStart += 4;
                }
                
                // Parse Content-Length header
                if (bodyStart > 0) {
                    size_t clPos = request.find("Content-Length:");
                    if (clPos != std::string::npos && clPos < bodyStart) {
                        size_t clValueStart = clPos + 15; // Skip "Content-Length:"
                        // Skip whitespace
                        while (clValueStart < request.size() && std::isspace(static_cast<unsigned char>(request[clValueStart]))) {
                            clValueStart++;
                        }
                        size_t clEnd = request.find("\r\n", clValueStart);
                        if (clEnd == std::string::npos) clEnd = request.find("\n", clValueStart);
                        if (clEnd != std::string::npos) {
                            std::string clStr = request.substr(clValueStart, clEnd - clValueStart);
                            contentLength = std::atoi(clStr.c_str());
                        }
                    }
                }
            }
            
            // Check if we've read the complete body
            if (contentLength >= 0 && bodyStart > 0) {
                int currentBodySize = request.size() - bodyStart;
                if (currentBodySize >= contentLength) {
                    break; // Got full body based on Content-Length
                }
            } else if (bodyStart > 0 && contentLength < 0) {
                // No Content-Length header - for multipart, read until closing boundary
                size_t contentTypePos = request.find("Content-Type:");
                if (contentTypePos != std::string::npos) {
                    size_t multipartPos = request.find("multipart/form-data", contentTypePos);
                    if (multipartPos != std::string::npos) {
                        // Find boundary and check for closing marker
                        size_t boundaryPos = request.find("boundary=", contentTypePos);
                        if (boundaryPos != std::string::npos) {
                            boundaryPos += 9;
                            while (boundaryPos < request.size() && (request[boundaryPos] == ' ' || request[boundaryPos] == '"')) {
                                boundaryPos++;
                            }
                            size_t boundaryEnd = boundaryPos;
                            while (boundaryEnd < request.size() && 
                                   request[boundaryEnd] != '\r' && 
                                   request[boundaryEnd] != '\n' && 
                                   request[boundaryEnd] != ';' &&
                                   request[boundaryEnd] != '"') {
                                boundaryEnd++;
                            }
                            std::string boundaryValue = request.substr(boundaryPos, boundaryEnd - boundaryPos);
                            std::string closingBoundary = "--" + boundaryValue + "--";
                            if (request.find(closingBoundary, bodyStart) != std::string::npos) {
                                break; // Got full multipart body
                            }
                        }
                    } else {
                        // Not multipart, assume complete if we have headers
                        break;
                    }
                } else {
                    // No Content-Type, assume complete
                    break;
                }
            }
            
            std::memset(buffer, 0, sizeof(buffer));
        }
        
        if (!request.empty()) {
            if (logger) logger->log(LogLevel::Debug, "Received request: " + request);
            std::string response = handleRequest(request);
            send(client_fd, response.c_str(), response.size(), 0);
        }
        close(client_fd);
    }

    close(server_fd);
}

std::string Server::handleRequest(const std::string& request) {
    std::istringstream requestStream(request);
    std::string method, path;
    requestStream >> method >> path;

    // Strip query parameters from path for matching
    std::string pathWithoutQuery = path;
    size_t queryPos = pathWithoutQuery.find('?');
    if (queryPos != std::string::npos) {
        pathWithoutQuery = pathWithoutQuery.substr(0, queryPos);
    }

    for (const auto& [endpoint, handler] : endpointHandlers) {
        auto spacePos = endpoint.find(' ');
        if (spacePos == std::string::npos) continue;
        std::string endpointMethod = endpoint.substr(0, spacePos);
        std::string endpointPath = endpoint.substr(spacePos + 1);

        if (endpointMethod != method) continue; // method must match exactly

        std::string endpointPattern = endpointPath;
        size_t pos;
        while ((pos = endpointPattern.find("{")) != std::string::npos) {
            size_t endPos = endpointPattern.find("}", pos);
            if (endPos != std::string::npos) {
                endpointPattern.replace(pos, endPos - pos + 1, "[^/]+");
            }
        }

        // Use regex to match the path only (not the method, and without query params)
        std::regex pattern(endpointPattern);
        if (std::regex_match(pathWithoutQuery, pattern)) {
            std::string body = handler(request);
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Content-Length: " << body.size() << "\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n";
            response << "Access-Control-Allow-Headers: Content-Type\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << body;
            return response.str();
        }
    }

    // Handle OPTIONS requests for CORS preflight
    if (method == "OPTIONS") {
        std::ostringstream response;
        response << "HTTP/1.1 204 No Content\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        return response.str();
    }

    // Default to 404 if no match is found
    std::ostringstream response;
    response << "HTTP/1.1 404 Not Found\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: 13\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << "404 Not Found";
    return response.str();
}

