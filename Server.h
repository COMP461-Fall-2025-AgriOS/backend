#pragma once

class Server {
public:
    Server();
    void start();
    void stop();
private:
    int port;
};