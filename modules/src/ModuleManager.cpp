#include "ModuleManager.h"
#include <algorithm>

ModuleManager& ModuleManager::instance() {
    static ModuleManager mgr;
    return mgr;
}

void ModuleManager::registerCallback(const std::string& moduleId, ModuleCallback cb) {
    std::lock_guard<std::mutex> g(mu);
    callbacks[moduleId] = std::move(cb);
}

void ModuleManager::unregisterCallback(const std::string& moduleId) {
    std::lock_guard<std::mutex> g(mu);
    callbacks.erase(moduleId);
}

bool ModuleManager::invoke(const std::string& moduleId, const std::string& context) {
    ModuleCallback cb;
    {
        std::lock_guard<std::mutex> g(mu);
        auto it = callbacks.find(moduleId);
        if (it == callbacks.end()) return false;
        cb = it->second; // copy out while holding lock
    }
    // invoke outside lock
    cb(context);
    return true;
}

void ModuleManager::invokeAll(const std::string& context) {
    std::vector<ModuleCallback> cbs;
    {
        std::lock_guard<std::mutex> g(mu);
        cbs.reserve(callbacks.size());
        for (auto &kv : callbacks) cbs.push_back(kv.second);
    }
    for (auto &cb : cbs) cb(context);
}

std::vector<std::string> ModuleManager::listRegistered() const {
    std::lock_guard<std::mutex> g(mu);
    std::vector<std::string> keys;
    keys.reserve(callbacks.size());
    for (auto &kv : callbacks) keys.push_back(kv.first);
    return keys;
}
