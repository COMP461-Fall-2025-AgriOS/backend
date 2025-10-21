#pragma once

#include <string>
#include <memory>

enum class LogLevel { Info, Warn, Error, Debug };

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(LogLevel level, const std::string& msg) = 0;
};

std::unique_ptr<Logger> makeConsoleLogger();
