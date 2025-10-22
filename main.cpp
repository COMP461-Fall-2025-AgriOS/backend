#include "Server.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string pluginsDir = "./plugins";

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--port" && i + 1 < argc) {
            port = std::atoi(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--plugins-dir" && i + 1 < argc) {
            pluginsDir = argv[i + 1];
        }
    }

    Server server(port);
    server.loadPluginsFromDirectory(pluginsDir);

    server.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}