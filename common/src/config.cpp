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

    if (j.contains("components")) {
        for (const auto& comp_json : j["components"]) {
            ComponentConfig comp;
            comp.name = comp_json.value("name", "unnamed");
            comp.type = comp_json.value("type", "unknown");
            comp.library_path = comp_json.value("library_path", "");
            comp.config = comp_json.value("config", json::object());
            
            config.components.push_back(comp);
        }
    }

    return config;
}
