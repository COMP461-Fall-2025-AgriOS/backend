#include "Logger.h"
#include <iostream>

class ConsoleLogger : public Logger {
public:
    void log(LogLevel level, const std::string& msg) override {
        switch (level) {
            case LogLevel::Info:  std::cout << "[INFO]  "; break;
            case LogLevel::Warn:  std::cout << "[WARN]  "; break;
            case LogLevel::Error: std::cout << "[ERROR] "; break;
            case LogLevel::Debug: std::cout << "[DEBUG] "; break;
        }
        std::cout << msg << std::endl;
    }
};

std::unique_ptr<Logger> makeConsoleLogger() {
    return std::make_unique<ConsoleLogger>();
}
