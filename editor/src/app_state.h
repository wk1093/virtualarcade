#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include <nlohmann/json.hpp>
#include "image_format.h"

using json = nlohmann::json;

// ============================================================
// Project: motherboard config + all ROM images for one session
// ============================================================

struct MemoryRegion {
    std::string type;         // "ram" or "rom"
    std::string name;         // e.g., "generic_ram"
    uint32_t start_address;
    uint32_t size;
    json     overrides;       // matching what runner/config.h uses
};

struct ComponentRef {
    std::string type;         // "cpu", "gpu", "ram", "rom"
    std::string name;
    json        overrides;
};

struct MotherboardConfig {
    std::string name         = "New System";
    std::string version      = "1.0.0";
    uint32_t    master_clock = 21477272;
    std::vector<ComponentRef> components;

    json to_json() const {
        json j;
        j["name"]         = name;
        j["version"]      = version;
        j["master_clock"] = master_clock;
        j["components"]   = json::array();
        for (const auto& c : components) {
            json cj;
            cj["type"]      = c.type;
            cj["name"]      = c.name;
            cj["overrides"] = c.overrides.is_null() ? json::object() : c.overrides;
            j["components"].push_back(cj);
        }
        return j;
    }

    static MotherboardConfig from_json(const json& j) {
        MotherboardConfig cfg;
        cfg.name         = j.value("name", "New System");
        cfg.version      = j.value("version", "1.0.0");
        cfg.master_clock = j.value("master_clock", (uint32_t)21477272);
        for (const auto& cj : j.value("components", json::array())) {
            ComponentRef c;
            c.type      = cj.value("type", "");
            c.name      = cj.value("name", "");
            c.overrides = cj.value("overrides", json::object());
            cfg.components.push_back(c);
        }
        return cfg;
    }
};

// A single ROM region's content, with metadata about how it was created
struct RomSlot {
    std::string name;           // human name, e.g. "PRG ROM"
    VarcadeImage::ImageType type = VarcadeImage::TYPE_ROM;
    std::vector<uint8_t>    data;
    bool                    modified = false;
    
    // source tracking (which tab produced this)
    enum class Source { Raw, Ascii, Bitmap, Code } source = Source::Raw;
};

// ============================================================
// Build log: collects messages from assembler/compiler runs
// ============================================================

struct LogEntry {
    enum class Level { Info, Warning, Error } level;
    std::string message;
};

struct BuildLog {
    std::vector<LogEntry> entries;

    void info   (const std::string& msg) { entries.push_back({LogEntry::Level::Info,    msg}); }
    void warn   (const std::string& msg) { entries.push_back({LogEntry::Level::Warning, msg}); }
    void error  (const std::string& msg) { entries.push_back({LogEntry::Level::Error,   msg}); }
    void clear  () { entries.clear(); }
    bool has_error() const {
        for (auto& e : entries) if (e.level == LogEntry::Level::Error) return true;
        return false;
    }
};

// ============================================================
// Central application state – shared between all panels
// ============================================================

struct AppState {
    // ---- Project files ----
    std::string project_dir;          // directory for all saved files
    std::string motherboard_file;     // .json path
    std::string image_file;           // .img path

    // ---- Motherboard config ----
    MotherboardConfig motherboard;
    bool mb_modified = false;

    // ---- ROM slots ----
    std::vector<RomSlot> rom_slots;
    int active_rom_idx = -1;          // which slot is shown in center editors

    // ---- Code editor ----
    // Each rom slot can carry its own code buffer and code source type
    std::vector<std::string>     code_buffers;     // per-slot code text
    std::vector<RomSlot::Source> code_sources;     // per-slot source type

    // ---- Build state ----
    BuildLog build_log;
    bool     build_running = false;

    // ---- UI state ----
    bool       show_motherboard_panel = true;
    bool       show_rom_panel         = true;
    bool       show_code_panel        = true;
    bool       show_build_panel       = true;
    bool       show_hex_panel         = true;

    // ---- Helpers ----

    void new_project() {
        motherboard = MotherboardConfig{};
        rom_slots.clear();
        code_buffers.clear();
        code_sources.clear();
        active_rom_idx = -1;
        mb_modified    = false;
        build_log.clear();
        project_dir.clear();
        motherboard_file.clear();
        image_file.clear();
    }

    void add_rom_slot(const std::string& name, VarcadeImage::ImageType type, uint32_t size) {
        RomSlot slot;
        slot.name = name;
        slot.type = type;
        slot.data.assign(size, 0);
        slot.modified = true;
        rom_slots.push_back(slot);
        code_buffers.push_back("");
        code_sources.push_back(RomSlot::Source::Raw);
        active_rom_idx = static_cast<int>(rom_slots.size()) - 1;
    }

    // Save motherboard config to JSON
    bool save_motherboard(const std::string& path) {
        try {
            json j = motherboard.to_json();
            std::ofstream f(path);
            if (!f) return false;
            f << j.dump(2);
            motherboard_file = path;
            mb_modified = false;
            return true;
        } catch (...) { return false; }
    }

    // Load motherboard config from JSON
    bool load_motherboard(const std::string& path) {
        try {
            std::ifstream f(path);
            if (!f) return false;
            json j;
            f >> j;
            motherboard = MotherboardConfig::from_json(j);
            motherboard_file = path;
            mb_modified = false;
            return true;
        } catch (...) { return false; }
    }

    // Save all ROM slots to a .img file
    bool save_image(const std::string& path) {
        try {
            VarcadeImage::ImageFile imgf;
            for (const auto& slot : rom_slots) {
                imgf.add_image(slot.name, slot.type, slot.data);
            }
            imgf.save(path);
            image_file = path;
            for (auto& s : rom_slots) s.modified = false;
            return true;
        } catch (...) { return false; }
    }

    // Load ROM slots from a .img file
    bool load_image(const std::string& path) {
        try {
            VarcadeImage::ImageFile imgf = VarcadeImage::ImageFile::load(path);
            rom_slots.clear();
            code_buffers.clear();
            code_sources.clear();
            for (size_t i = 0; i < imgf.headers.size(); i++) {
                RomSlot slot;
                slot.name = imgf.headers[i].name;
                slot.type = imgf.headers[i].type;
                slot.data = imgf.images[i];
                rom_slots.push_back(slot);
                code_buffers.push_back("");
                code_sources.push_back(RomSlot::Source::Raw);
            }
            image_file = path;
            active_rom_idx = rom_slots.empty() ? -1 : 0;
            return true;
        } catch (...) { return false; }
    }
};

#include <fstream>
