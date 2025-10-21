#pragma once

#include <string>
#include <vector>

struct Module {
    std::string id; // UUID string
    std::string name;
    std::string description;
    bool enabled = false;

    std::string serialize() const;
    static Module deserialize(const std::string& data);
    static std::vector<Module> deserializeList(const std::string& data);
};
