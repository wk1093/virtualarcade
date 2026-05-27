#include "config.h"
#include <fstream>
#include <iostream>

MotherboardConfig MotherboardConfig::from_json_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open configuration file: " + filepath);
    }

    json j;
    file >> j;
    file.close();

    return from_json(j);
}

MotherboardConfig MotherboardConfig::from_json(const json& j) {
    MotherboardConfig config;
    
    config.name = j.value("name", "Unnamed Motherboard");
    config.version = j.value("version", "1.0.0");
    config.master_clock = j.value("master_clock", 1000000);

    // Parse component references
    if (j.contains("components")) {
        for (const auto& comp_json : j["components"]) {
            ComponentRef comp;
            comp.type = comp_json.value("type", "unknown");
            comp.name = comp_json.value("name", "unnamed");
            comp.overrides = comp_json.value("overrides", json::object());
            
            config.components.push_back(comp);
        }
    }

    return config;
}
