#include "Server.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <asio.hpp>

using asio::ip::tcp;

Server::Server(int port) : port(port), running(false) {}

Server::~Server() {
    stop();
}

void Server::initializeHandlers() {
    registerEndpoint("POST /robots/{id}", [](const std::string& request) {
        return "Handler for POST /robots/{id}";
    });

    registerEndpoint("PATCH /robots/{id}", [](const std::string& request) {
        return "Handler for PATCH /robots/{id}";
    });

    registerEndpoint("GET /robots/", [](const std::string& request) {
        return "Handler for GET /robots/";
    });

    registerEndpoint("GET /robots/{id}", [](const std::string& request) {
        return "Handler for GET /robots/{id}";
    });

    registerEndpoint("DELETE /robots/{id}", [](const std::string& request) {
        return "Handler for DELETE /robots/{id}";
    });

    registerEndpoint("DELETE /robots/", [](const std::string& request) {
        return "Handler for DELETE /robots/";
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
    try {
        asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));

        while (running) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::array<char, 1024> buffer;
            asio::error_code error;
            size_t length = socket.read_some(asio::buffer(buffer), error);

            if (error == asio::error::eof) {
                break;
            } else if (error) {
                throw asio::system_error(error);
            }

            std::string request(buffer.data(), length);
            std::string response = handleRequest(request);

            asio::write(socket, asio::buffer(response), error);
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in server: " << e.what() << std::endl;
    }
}

std::string Server::handleRequest(const std::string& request) {
    auto it = endpointHandlers.find(request);
    if (it != endpointHandlers.end()) {
        return it->second(request);
    }
    return "404 Not Found";
}

