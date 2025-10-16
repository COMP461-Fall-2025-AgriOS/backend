#include "Module.h"
#include <sstream>
#include <regex>

static std::string extractString(const std::string& s, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(s, m, re)) return m[1];
    return std::string();
}

static bool extractBool(const std::string& s, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (std::regex_search(s, m, re)) return m[1] == "true";
    return false;
}

std::string Module::serialize() const {
    std::ostringstream out;
    out << '{';
    out << "\"id\":\"" << id << "\",";
    out << "\"name\":\"" << name << "\",";
    out << "\"description\":\"" << description << "\",";
    out << "\"enabled\":" << (enabled ? "true" : "false");
    out << '}';
    return out.str();
}

Module Module::deserialize(const std::string& data) {
    Module m;
    m.id = extractString(data, "id");
    m.name = extractString(data, "name");
    m.description = extractString(data, "description");
    m.enabled = extractBool(data, "enabled");
    return m;
}

std::vector<Module> Module::deserializeList(const std::string& data) {
    std::vector<Module> out;
    size_t pos = 0;
    while ((pos = data.find('{', pos)) != std::string::npos) {
        size_t end = data.find('}', pos);
        if (end == std::string::npos) break;
        out.push_back(deserialize(data.substr(pos, end - pos + 1)));
        pos = end + 1;
    }
    return out;
}
