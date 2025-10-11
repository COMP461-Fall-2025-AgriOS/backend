#include "Server.h"
#include "Robot.h"
#include "Map.h"
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

Server::Server(int port) : port(port), running(false) {}

Server::~Server() {
    stop();
}

// Add maps and robots lists
std::unordered_map<int, Robot> robots;
std::unordered_map<int, Map> maps;

void Server::initializeHandlers() {
    registerEndpoint("POST /robots/{id}", [this](const std::string& request) {
        // Extract robot data from request and add to storage
        std::istringstream requestStream(request);
        std::string method, path, body;
        requestStream >> method >> path;
        std::getline(requestStream, body);

        Robot newRobot = Robot::deserialize(body);
        robots[newRobot.id] = newRobot;

        return "Robot created successfully\n";
    });

    registerEndpoint("POST /robots", [this](const std::string& request) {
        // Extract robots data from request and add to storage
        std::istringstream requestStream(request);
        std::string method, path, body;
        requestStream >> method >> path;
        std::getline(requestStream, body);

        std::vector<Robot> newRobots = Robot::deserializeList(body);
        for (const auto& robot : newRobots) {
            robots[robot.id] = robot;
        }

        return "Robots created successfully\n";
    });

    registerEndpoint("PATCH /robots/{id}", [this](const std::string& request) {
        // Extract robot ID and update its attributes
        std::istringstream requestStream(request);
        std::string method, path, body;
        requestStream >> method >> path;
        std::getline(requestStream, body);

        std::regex idRegex("PATCH /robots/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (robots.find(id) != robots.end()) {
                robots[id] = Robot::deserialize(body);
                return "Robot updated successfully\n";
            }
        }

        return "Robot not found\n";
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
                return std::string(robots[id].serialize());
            }
        }

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
                return "Robot deleted successfully\n";
            }
        }

        return "Robot not found\n";
    });

    registerEndpoint("DELETE /robots", [this](const std::string& request) {
        // Delete all robots
        robots.clear();
        return "All robots deleted successfully\n";
    });

    registerEndpoint("POST /map/{id}", [this](const std::string& request) {
        // Create map
        std::istringstream requestStream(request);
        std::string method, path, body;
        requestStream >> method >> path;
        std::getline(requestStream, body);

        std::regex idRegex("POST /map/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            // maps[id] = Map::deserialize(body);
            return "Map created successfully\n";
        }

        return "Failed to create map\n";
    });

    registerEndpoint("PATCH /map/{id}", [this](const std::string& request) {
        // Update map
        std::istringstream requestStream(request);
        std::string method, path, body;
        requestStream >> method >> path;
        std::getline(requestStream, body);

        std::regex idRegex("PATCH /map/([0-9]+)");
        std::smatch match;
        if (std::regex_search(path, match, idRegex)) {
            int id = std::stoi(match[1]);
            if (maps.find(id) != maps.end()) {
                // maps[id] = Map::deserialize(body);
                return "Map updated successfully\n";
            }
        }

        return "Map not found\n";
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
                return "Map deleted successfully\n";
            }
        }

        return "Map not found\n";
    });
}

void Server::start() {
    initializeHandlers();
    running = true;
    serverThread = std::thread(&Server::run, this);
    std::cout << "Server started on port " << port << std::endl;
}

void Server::stop() {
    running = false;
    if (serverThread.joinable()) {
        serverThread.join();
    }
    std::cout << "Server stopped." << std::endl;
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
            if (running) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }

        std::cout << "New client accepted from "
                  << inet_ntoa(client_address.sin_addr) << ":"
                  << ntohs(client_address.sin_port) << std::endl;

        char buffer[1024] = {0};
        int bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            std::string request(buffer, bytes_read);
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

