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

Server::Server(int port) : port(port), running(false) {
    // default logger
    logger = makeConsoleLogger();
}

Server::Server(int port, std::unique_ptr<Logger> inLogger) : port(port), running(false), logger(std::move(inLogger)) {}

Server::~Server() {
    stop();
}

std::unordered_map<std::string, Robot> robots;
std::unordered_map<std::string, Map> maps;
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

        if (logger) logger->log(LogLevel::Info, std::string("Created robot id=") + newRobot.id);
        return std::string("Robot created successfully\n");
    });

    registerEndpoint("POST /robots", [this](const std::string& request) {
        std::string body = extractBody(request);

        std::vector<Robot> newRobots = Robot::deserializeList(body);
        for (const auto& robot : newRobots) {
            robots[robot.id] = robot;
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
            if (robots.find(id) != robots.end()) {
                robots[id] = Robot::deserialize(body);
                if (logger) logger->log(LogLevel::Info, "Updated robot id=" + id);
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
            if (robots.erase(id)) {
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
        return std::string("Failed to create map\n");
    });

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

    registerEndpoint("GET /map/{id}", [this](const std::string& request) {
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
        return std::string("Map not found\n");
    });

    registerEndpoint("GET /map/", [this](const std::string& request) {
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

        return result;
    });

    registerEndpoint("DELETE /map/{id}", [this](const std::string& request) {
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("/map/([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            std::string id = match[1];
            if (maps.erase(id)) {
                if (logger) logger->log(LogLevel::Info, std::string("Deleted map id=") + id);
                return std::string("Map deleted successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Delete map not found");
        return std::string("Map not found\n");
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
    if (logger) logger->log(LogLevel::Info, "Server stopped.");
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

        char buffer[1024] = {0};
        int bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            std::string request(buffer, bytes_read);
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

        // Use regex to match the path only (not the method)
        std::regex pattern(endpointPattern);
        if (std::regex_match(path, pattern)) {
            std::string body = handler(request);
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Content-Length: " << body.size() << "\r\n";
            response << "Connection: close\r\n";
            response << "\r\n";
            response << body;
            return response.str();
        }
    }

    // Default to 404 if no match is found
    std::ostringstream response;
    response << "HTTP/1.1 404 Not Found\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: 13\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << "404 Not Found";
    return response.str();
}

