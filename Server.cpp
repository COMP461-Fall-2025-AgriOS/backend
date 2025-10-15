#include "Server.h"
#include "Robot.h"
#include "Map.h"
#include "Logger.h"
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

// Add maps and robots lists
std::unordered_map<int, Robot> robots;
std::unordered_map<int, Map> maps;

// Helper function to extract body from HTTP request
std::string extractBody(const std::string& request) {
    // Find the double newline that separates headers from body
    size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        return request.substr(bodyStart + 4);
    }
    
    // Try single newline separator as fallback
    bodyStart = request.find("\n\n");
    if (bodyStart != std::string::npos) {
        return request.substr(bodyStart + 2);
    }
    
    return "";
}

void Server::initializeHandlers() {
    registerEndpoint("POST /robots/{id}", [this](const std::string& request) {
        // Extract robot data from request and add to storage
        std::string body = extractBody(request);

        Robot newRobot = Robot::deserialize(body);
        robots[newRobot.id] = newRobot;

        if (logger) logger->log(LogLevel::Info, "Created robot id=" + std::to_string(newRobot.id));
        return std::string("Robot created successfully\n");
    });

    registerEndpoint("POST /robots", [this](const std::string& request) {
        // Extract robots data from request and add to storage
        std::string body = extractBody(request);

        std::vector<Robot> newRobots = Robot::deserializeList(body);
        for (const auto& robot : newRobots) {
            robots[robot.id] = robot;
        }

        if (logger) logger->log(LogLevel::Info, "Created " + std::to_string(newRobots.size()) + " robots");
        return std::string("Robots created successfully\n");
    });

    registerEndpoint("PATCH /robots/{id}", [this](const std::string& request) {
        // Extract robot ID and update its attributes
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("PATCH /robots/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (robots.find(id) != robots.end()) {
                robots[id] = Robot::deserialize(body);
                if (logger) logger->log(LogLevel::Info, "Updated robot id=" + std::to_string(id));
                return std::string("Robot updated successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Patch robot not found");
        return std::string("Robot not found\n");
    });

    registerEndpoint("GET /robots", [this](const std::string& request) {
        // Return all robots as JSON
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
        // Return specific robot as JSON
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("GET /robots/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (robots.find(id) != robots.end()) {
                if (logger) logger->log(LogLevel::Info, "Fetched robot id=" + std::to_string(id));
                return std::string(robots[id].serialize());
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Get robot not found");
        return std::string("Robot not found\n");
    });

    registerEndpoint("DELETE /robots/{id}", [this](const std::string& request) {
        // Delete specific robot
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("DELETE /robots/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (robots.erase(id)) {
                if (logger) logger->log(LogLevel::Info, "Deleted robot id=" + std::to_string(id));
                return std::string("Robot deleted successfully\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Delete robot not found");
        return std::string("Robot not found\n");
    });

    registerEndpoint("DELETE /robots", [this](const std::string& request) {
        // Delete all robots
        robots.clear();
        if (logger) logger->log(LogLevel::Info, "Deleted all robots");
        return std::string("All robots deleted successfully\n");
    });

    registerEndpoint("POST /map/{id}", [this](const std::string& request) {
        // Create map
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
    std::string body = extractBody(request);
    if (logger) logger->log(LogLevel::Debug, std::string("Received map body: ") + body + std::string(" Path: ") + path + std::string(" Method: ") + method);

        std::regex idRegex("POST /map/[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            
            // Parse width and height from JSON body
            std::regex widthRegex("\"width\"\\s*:\\s*([0-9]+)");
            std::regex heightRegex("\"height\"\\s*:\\s*([0-9]+)");
            std::smatch widthMatch, heightMatch;
            
            if (std::regex_search(body, widthMatch, widthRegex) && 
                std::regex_search(body, heightMatch, heightRegex)) {
                int width = std::stoi(widthMatch[1]);
                int height = std::stoi(heightMatch[1]);
                
                // Create and store the map
                maps.emplace(id, Map(width, height));
                if (logger) logger->log(LogLevel::Info, "Created map with id=" + std::to_string(id) + ", width=" + std::to_string(width) + ", height=" + std::to_string(height));

                return std::string("Map created successfully\n");
            } else {
                return std::string("Failed to parse map dimensions\n");
            }
        }

        if (logger) logger->log(LogLevel::Warn, "Failed to create map (bad path)");
        return std::string("Failed to create map\n");
    });

    registerEndpoint("PATCH /map/{id}", [this](const std::string& request) {
        // Update map
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;
        
        std::string body = extractBody(request);

        std::regex idRegex("PATCH /map/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (maps.find(id) != maps.end()) {
                // TODO: Implement Map::deserialize or parse JSON body
                // maps[id] = Map::deserialize(body);
                if (logger) logger->log(LogLevel::Info, "Updated map id=" + std::to_string(id));
                return std::string("Map updated successfully\n");
            }
        }

        return std::string("Map not found\n");
    });

    registerEndpoint("GET /map", [this](const std::string& request) {
        // Return all maps as JSON
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
        // Delete map
        std::istringstream requestStream(request);
        std::string method, path;
        requestStream >> method >> path;

        std::regex idRegex("DELETE /map/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (maps.erase(id)) {
                if (logger) logger->log(LogLevel::Info, "Deleted map id=" + std::to_string(id));
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
        std::string endpointPattern = endpoint;
        size_t pos;
        while ((pos = endpointPattern.find("{")) != std::string::npos) {
            size_t endPos = endpointPattern.find("}", pos);
            if (endPos != std::string::npos) {
                endpointPattern.replace(pos, endPos - pos + 1, "[^/]+");
            }
        }

        // Use regex to match the path
        std::regex pattern(endpointPattern);
        if (std::regex_match(method + " " + path, pattern)) {
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

