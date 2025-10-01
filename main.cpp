#include "Server.h"

int main() {
    Server server(8080); // Start the server on port 8080
    server.start();

    // Keep the server running until manually stopped
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}