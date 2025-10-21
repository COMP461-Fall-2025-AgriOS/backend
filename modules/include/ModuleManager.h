#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

using ModuleCallback = std::function<void(const std::string& context)>;

class ModuleManager {
public:
    static ModuleManager& instance();

    void registerCallback(const std::string& moduleId, ModuleCallback cb);
    void unregisterCallback(const std::string& moduleId);

    bool invoke(const std::string& moduleId, const std::string& context);

    void invokeAll(const std::string& context);

    std::vector<std::string> listRegistered() const;

private:
    ModuleManager() = default;
    mutable std::mutex mu;
    std::unordered_map<std::string, ModuleCallback> callbacks;
};
