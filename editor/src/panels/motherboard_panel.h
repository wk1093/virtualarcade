#pragma once
#include <imgui.h>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "app_state.h"

// ============================================================
// Motherboard Designer Panel
// Left-side panel – shows and edits the motherboard config
// ============================================================

namespace MotherboardPanel {

static char  mb_name_buf[256]    = {};
static char  mb_ver_buf[64]      = {};
static int   mb_clock            = 21477272;
static bool  needs_sync          = true;  // sync AppState → buffers once

// Available component types for the "Add Component" dropdown
static const char* COMP_TYPES[] = { "cpu", "gpu", "ram", "rom" };
static int  add_type_idx = 0;
static int  add_name_idx = 0;
static int  last_add_type_idx = 0;

static char add_param_key_buf[128] = "";
static char add_param_str_buf[256] = "";
static int  add_param_int_val = 0;
static float add_param_float_val = 0.0f;
static bool add_param_bool_val = false;
static int  add_param_type_idx = 0; // 0=string, 1=int, 2=float, 3=bool
static int  add_param_name_idx = 0;

static const char* PARAM_TYPES[] = { "String", "Integer", "Float", "Boolean" };
static const char* PARAM_PRESETS[] = {
    "start_address", "size", "clock_divider", "bank", "address_mask", "custom"
};

namespace fs = std::filesystem;

static std::map<std::string, std::vector<std::string>> g_component_names;
static std::map<std::string, std::map<std::string, json>> g_component_defaults;
static bool g_component_catalog_ready = false;

inline bool has_string(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

inline void add_name_for_type(const std::string& type, const std::string& name) {
    auto& names = g_component_names[type];
    if (!has_string(names, name)) names.push_back(name);
}

inline void load_defaults_file(const std::string& type, const std::string& name, const fs::path& json_path) {
    if (!fs::exists(json_path)) return;
    try {
        std::ifstream f(json_path);
        if (!f) return;
        json j;
        f >> j;
        g_component_defaults[type][name] = j.value("defaults", json::object());
    } catch (...) {
        // Ignore malformed definition files and keep UI responsive.
    }
}

inline void scan_component_dir(const std::string& type, const fs::path& root) {
    if (!fs::exists(root) || !fs::is_directory(root)) return;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        add_name_for_type(type, name);

        fs::path definition;
        if (type == "cpu")      definition = entry.path() / "data" / "cpu.json";
        else if (type == "gpu") definition = entry.path() / "data" / "gpu.json";
        else if (type == "ram") definition = entry.path() / "ram.json";
        else if (type == "rom") definition = entry.path() / "rom.json";

        load_defaults_file(type, name, definition);
    }
}

inline void ensure_component_catalog_loaded() {
    if (g_component_catalog_ready) return;

    // Source layout candidates.
    scan_component_dir("cpu", fs::path("cpus"));
    scan_component_dir("cpu", fs::path("../cpus"));
    scan_component_dir("cpu", fs::path("../../cpus"));

    scan_component_dir("gpu", fs::path("gpus"));
    scan_component_dir("gpu", fs::path("../gpus"));
    scan_component_dir("gpu", fs::path("../../gpus"));

    scan_component_dir("ram", fs::path("common/data/ram"));
    scan_component_dir("ram", fs::path("../common/data/ram"));
    scan_component_dir("ram", fs::path("../../common/data/ram"));
    scan_component_dir("ram", fs::path("data/ram"));

    scan_component_dir("rom", fs::path("common/data/rom"));
    scan_component_dir("rom", fs::path("../common/data/rom"));
    scan_component_dir("rom", fs::path("../../common/data/rom"));
    scan_component_dir("rom", fs::path("data/rom"));

    // Robust fallback for early-stage projects.
    add_name_for_type("cpu", "generic_cpu");
    add_name_for_type("gpu", "generic_gpu");
    add_name_for_type("ram", "generic_ram");
    add_name_for_type("rom", "generic_rom");

    if (!g_component_defaults["cpu"].count("generic_cpu")) {
        g_component_defaults["cpu"]["generic_cpu"] = json::object({
            {"reset_vector", "0x8000"}, {"speed_ratio", 1.0}
        });
    }
    if (!g_component_defaults["gpu"].count("generic_gpu")) {
        g_component_defaults["gpu"]["generic_gpu"] = json::object({
            {"resolution_w", 256}, {"resolution_h", 240}, {"speed_ratio", 0.33}
        });
    }
    if (!g_component_defaults["ram"].count("generic_ram")) {
        g_component_defaults["ram"]["generic_ram"] = json::object({
            {"size", 2048}, {"start_address", 0}, {"concurrent_read_ports", 1},
            {"concurrent_write_ports", 1}, {"access_time_ns", 150}
        });
    }
    if (!g_component_defaults["rom"].count("generic_rom")) {
        g_component_defaults["rom"]["generic_rom"] = json::object({
            {"size", 32768}, {"start_address", 32768}, {"access_time_ns", 200}
        });
    }

    g_component_catalog_ready = true;
}

inline const std::vector<std::string>& names_for_type(const std::string& type) {
    ensure_component_catalog_loaded();
    auto it = g_component_names.find(type);
    if (it != g_component_names.end()) return it->second;
    static const std::vector<std::string> empty;
    return empty;
}

inline json defaults_for_component(const std::string& type, const std::string& name) {
    ensure_component_catalog_loaded();
    auto type_it = g_component_defaults.find(type);
    if (type_it == g_component_defaults.end()) return json::object();
    auto comp_it = type_it->second.find(name);
    if (comp_it == type_it->second.end()) return json::object();
    return comp_it->second.is_object() ? comp_it->second : json::object();
}

inline void render_overrides_editor(const std::string& type, const std::string& name, json& overrides, bool& modified_flag) {
    if (!overrides.is_object()) overrides = json::object();
    json defaults = defaults_for_component(type, name);

    std::vector<std::string> keys_to_remove;

    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        const std::string key = it.key();
        json& value = it.value();

        ImGui::PushID(key.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(key.c_str());
        ImGui::SameLine(160);

        ImGui::SetNextItemWidth(170);
        if (value.is_boolean()) {
            bool v = value.get<bool>();
            if (ImGui::Checkbox("##bool", &v)) {
                value = v;
                modified_flag = true;
            }
        } else if (value.is_number_integer() || value.is_number_unsigned()) {
            int v = value.get<int>();
            if (ImGui::InputInt("##int", &v)) {
                value = v;
                modified_flag = true;
            }
        } else if (value.is_number_float()) {
            float v = value.get<float>();
            if (ImGui::InputFloat("##float", &v, 0.0f, 0.0f, "%.3f")) {
                value = v;
                modified_flag = true;
            }
        } else {
            std::string text = value.is_string() ? value.get<std::string>() : value.dump();
            char text_buf[256];
            std::strncpy(text_buf, text.c_str(), sizeof(text_buf) - 1);
            text_buf[sizeof(text_buf) - 1] = '\0';
            if (ImGui::InputText("##str", text_buf, sizeof(text_buf))) {
                value = std::string(text_buf);
                modified_flag = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            keys_to_remove.push_back(key);
        }
        if (defaults.contains(key)) {
            ImGui::SameLine();
            std::string d = defaults[key].dump();
            ImGui::TextDisabled("default: %s", d.c_str());
        }
        ImGui::PopID();
    }

    for (const auto& k : keys_to_remove) {
        overrides.erase(k);
        modified_flag = true;
    }

    std::vector<std::string> param_options;
    for (int i = 0; i < 5; ++i) param_options.push_back(PARAM_PRESETS[i]);
    if (defaults.is_object()) {
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (!has_string(param_options, it.key())) param_options.push_back(it.key());
        }
    }
    param_options.push_back("custom");
    if (add_param_name_idx >= (int)param_options.size()) add_param_name_idx = 0;

    ImGui::SeparatorText("Add Parameter");
    if (ImGui::BeginCombo("Parameter", param_options[add_param_name_idx].c_str())) {
        for (int i = 0; i < (int)param_options.size(); ++i) {
            bool selected = (i == add_param_name_idx);
            if (ImGui::Selectable(param_options[i].c_str(), selected)) add_param_name_idx = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (param_options[add_param_name_idx] == "custom") {
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("Custom Key", add_param_key_buf, sizeof(add_param_key_buf));
    } else if (defaults.contains(param_options[add_param_name_idx])) {
        std::string d = defaults[param_options[add_param_name_idx]].dump();
        ImGui::TextDisabled("Default: %s", d.c_str());
        if (ImGui::SmallButton("Use Default")) {
            const json dv = defaults[param_options[add_param_name_idx]];
            if (dv.is_boolean()) {
                add_param_type_idx = 3;
                add_param_bool_val = dv.get<bool>();
            } else if (dv.is_number_integer() || dv.is_number_unsigned()) {
                add_param_type_idx = 1;
                add_param_int_val = dv.get<int>();
            } else if (dv.is_number_float()) {
                add_param_type_idx = 2;
                add_param_float_val = dv.get<float>();
            } else {
                add_param_type_idx = 0;
                std::string s = dv.is_string() ? dv.get<std::string>() : dv.dump();
                std::strncpy(add_param_str_buf, s.c_str(), sizeof(add_param_str_buf) - 1);
                add_param_str_buf[sizeof(add_param_str_buf) - 1] = '\0';
            }
        }
    }
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("Type", &add_param_type_idx, PARAM_TYPES, 4);

    ImGui::SetNextItemWidth(180);
    if (add_param_type_idx == 0) {
        ImGui::InputText("Value", add_param_str_buf, sizeof(add_param_str_buf));
    } else if (add_param_type_idx == 1) {
        ImGui::InputInt("Value", &add_param_int_val);
    } else if (add_param_type_idx == 2) {
        ImGui::InputFloat("Value", &add_param_float_val, 0.0f, 0.0f, "%.3f");
    } else {
        ImGui::Checkbox("Value", &add_param_bool_val);
    }

    if (ImGui::Button("Add Parameter")) {
        std::string selected_param = param_options[add_param_name_idx];
        std::string key = (selected_param == "custom")
            ? std::string(add_param_key_buf)
            : selected_param;
        if (!key.empty()) {
            if (add_param_type_idx == 0) {
                overrides[key] = std::string(add_param_str_buf);
            } else if (add_param_type_idx == 1) {
                overrides[key] = add_param_int_val;
            } else if (add_param_type_idx == 2) {
                overrides[key] = add_param_float_val;
            } else {
                overrides[key] = add_param_bool_val;
            }
            add_param_key_buf[0] = '\0';
            add_param_str_buf[0] = '\0';
            modified_flag = true;
        }
    }
}

inline void sync_from_state(AppState& s) {
    strncpy(mb_name_buf, s.motherboard.name.c_str(), sizeof(mb_name_buf)-1);
    strncpy(mb_ver_buf,  s.motherboard.version.c_str(), sizeof(mb_ver_buf)-1);
    mb_clock = static_cast<int>(s.motherboard.master_clock);
    needs_sync = false;
}

inline void render(AppState& s) {
    if (needs_sync) sync_from_state(s);
    ensure_component_catalog_loaded();

    // ---- Header controls ----
    ImGui::SeparatorText("System Info");
    if (ImGui::InputText("Name", mb_name_buf, sizeof(mb_name_buf)))
        { s.motherboard.name = mb_name_buf; s.mb_modified = true; }
    if (ImGui::InputText("Version", mb_ver_buf, sizeof(mb_ver_buf)))
        { s.motherboard.version = mb_ver_buf; s.mb_modified = true; }
    if (ImGui::DragInt("Master Clock (Hz)", &mb_clock, 1000, 100000, 200000000))
        { s.motherboard.master_clock = static_cast<uint32_t>(mb_clock); s.mb_modified = true; }

    // ---- Component list ----
    ImGui::SeparatorText("Components");

    int to_remove = -1;
    for (int i = 0; i < (int)s.motherboard.components.size(); i++) {
        auto& c = s.motherboard.components[i];
        ImGui::PushID(i);

        if (ImGui::CollapsingHeader(("[" + c.type + "] " + c.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            render_overrides_editor(c.type, c.name, c.overrides, s.mb_modified);
        }

        if (ImGui::SmallButton("Remove Component")) to_remove = i;

        ImGui::PopID();
        ImGui::Separator();
    }

    if (to_remove >= 0) {
        s.motherboard.components.erase(s.motherboard.components.begin() + to_remove);
        s.mb_modified = true;
    }

    // ---- Add component ----
    ImGui::SeparatorText("Add Component");
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("Type##add", &add_type_idx, COMP_TYPES, 4);
    if (add_type_idx != last_add_type_idx) {
        add_name_idx = 0;
        last_add_type_idx = add_type_idx;
    }

    const std::string selected_type = COMP_TYPES[add_type_idx];
    const auto& names = names_for_type(selected_type);
    if (!names.empty()) {
        if (add_name_idx >= (int)names.size()) add_name_idx = 0;
        ImGui::SameLine();
        if (ImGui::BeginCombo("Name##add", names[add_name_idx].c_str())) {
            for (int i = 0; i < (int)names.size(); ++i) {
                bool selected = (i == add_name_idx);
                if (ImGui::Selectable(names[i].c_str(), selected)) add_name_idx = i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("No %s definitions found", selected_type.c_str());
    }

    if (ImGui::Button("Add Component")) {
        if (names.empty()) {
            s.build_log.warn("No component definitions found for type: " + selected_type);
            return;
        }
        ComponentRef c;
        c.type = selected_type;
        c.name = names[add_name_idx];
        c.overrides = defaults_for_component(c.type, c.name);
        s.motherboard.components.push_back(c);
        s.mb_modified = true;
    }

    // ---- Memory Map visualizer ----
    if (!s.motherboard.components.empty()) {
        ImGui::SeparatorText("Memory Map");
        for (const auto& c : s.motherboard.components) {
            if (c.type == "ram" || c.type == "rom") {
                uint32_t start = c.overrides.value("start_address", (uint32_t)0);
                uint32_t size  = c.overrides.value("size", (uint32_t)0);
                if (size == 0) {
                    ImGui::TextDisabled("[%s/%s] set start_address and size", c.type.c_str(), c.name.c_str());
                    continue;
                }
                ImVec4 color = (c.type == "rom")
                    ? ImVec4(0.3f, 0.7f, 0.3f, 1.0f)
                    : ImVec4(0.3f, 0.5f, 0.9f, 1.0f);
                ImGui::TextColored(color, "0x%04X - 0x%04X  [%s/%s]  %u B",
                    start, start + size - 1,
                    c.type.c_str(), c.name.c_str(), size);
            }
        }
    }

    // ---- Save / Load buttons ----
    ImGui::Separator();
    if (ImGui::Button("Save Config")) {
        if (!s.motherboard_file.empty()) {
            if (s.save_motherboard(s.motherboard_file))
                s.build_log.info("Saved motherboard to " + s.motherboard_file);
            else
                s.build_log.error("Failed to save motherboard");
        } else {
            s.build_log.warn("Use File -> Save Motherboard As... to choose a file first.");
        }
    }
    ImGui::SameLine();
    if (s.mb_modified) {
        ImGui::TextColored(ImVec4(1,0.7f,0,1), "*unsaved");
    }
}

} // namespace MotherboardPanel
