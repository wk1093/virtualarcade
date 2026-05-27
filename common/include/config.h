#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Represents a component reference in the motherboard (with optional overrides)
struct ComponentRef {
    std::string type;        // "cpu", "gpu", "ram", "rom"
    std::string name;        // e.g., "generic_cpu", "generic_ram"
    json overrides;          // Optional overrides to defaults from spec
};

// Represents the motherboard configuration
struct MotherboardConfig {
    std::string name;
    std::string version;
    uint32_t master_clock;   // Hz
    std::vector<ComponentRef> components;

    static MotherboardConfig from_json_file(const std::string& filepath);
    static MotherboardConfig from_json(const json& j);
};
