#include "Server.h"
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

void Server::initializeHandlers() {
    registerEndpoint("POST /robots/{id}", [](const std::string& request) {
        return "Handler for POST /robots/{id}\n";
    });

    registerEndpoint("PATCH /robots/{id}", [](const std::string& request) {
        return "Handler for PATCH /robots/{id}\n";
    });

    registerEndpoint("GET /robots/", [](const std::string& request) {
        return "Handler for GET /robots/\n";
    });

    registerEndpoint("GET /robots/{id}", [](const std::string& request) {
        return "Handler for GET /robots/{id}\n";
    });

    registerEndpoint("DELETE /robots/{id}", [](const std::string& request) {
        return "Handler for DELETE /robots/{id}\n";
    });

    registerEndpoint("DELETE /robots/", [](const std::string& request) {
        return "Handler for DELETE /robots/\n";
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

