#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Represents a component in the motherboard
struct ComponentConfig {
    std::string name;
    std::string type;        // "cpu", "gpu", etc.
    std::string library_path;
    json config;
};

// Represents the motherboard configuration
struct MotherboardConfig {
    std::string name;
    std::string version;
    uint32_t master_clock;   // Hz
    std::vector<ComponentConfig> components;

    static MotherboardConfig from_json_file(const std::string& filepath);
    static MotherboardConfig from_json(const json& j);
};
