#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <limits.h>
#include <dlfcn.h>
#include <unistd.h>
#include "app_state.h"
#include "arcade_interface.h"

// ============================================================
// Code Editor Panel + Build System
// Centre-top panel – multi-line source editor with ASM/C tabs
// Build panel is rendered inside this file (bottom strip)
// ============================================================

namespace CodePanel {

// Per-slot static text buffers (heap-allocated, one per slot)
static std::vector<std::vector<char>> slot_bufs;
static int  buf_slot_version = -1; // trigger rebuild when slot count changes
static const int CODE_BUF_SIZE = 64 * 1024;

inline void ensure_buffers(AppState& s) {
    if ((int)slot_bufs.size() != (int)s.rom_slots.size() ||
        buf_slot_version != (int)s.rom_slots.size()) {
        slot_bufs.resize(s.rom_slots.size());
        for (size_t i = 0; i < slot_bufs.size(); i++) {
            if (slot_bufs[i].empty()) {
                slot_bufs[i].assign(CODE_BUF_SIZE, '\0');
                // Seed from existing code_buffers
                if (i < s.code_buffers.size() && !s.code_buffers[i].empty()) {
                    size_t n = std::min(s.code_buffers[i].size(), (size_t)CODE_BUF_SIZE - 1);
                    std::memcpy(slot_bufs[i].data(), s.code_buffers[i].c_str(), n);
                    slot_bufs[i][n] = '\0';
                }
            }
        }
        buf_slot_version = (int)s.rom_slots.size();
    }
}

namespace fs = std::filesystem;

struct CpuToolchainHandle {
    void* so_handle = nullptr;
    CPU_Interface* cpu_iface = nullptr;
    std::string cpu_name;
    std::string so_path;
    std::string last_error;
};

static CpuToolchainHandle g_toolchain;
static int g_language_idx = 0;

inline void unload_toolchain() {
    if (g_toolchain.so_handle) dlclose(g_toolchain.so_handle);
    g_toolchain = CpuToolchainHandle{};
    g_language_idx = 0;
}

inline std::string get_executable_dir() {
    char exe_path[PATH_MAX] = {};
    ssize_t cnt = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (cnt > 0) {
        std::string full(exe_path, cnt);
        auto slash = full.find_last_of('/');
        if (slash != std::string::npos) return full.substr(0, slash);
    }
    return ".";
}

inline std::string get_active_cpu_name(const AppState& s) {
    for (const auto& c : s.motherboard.components) {
        if (c.type == "cpu" && !c.name.empty()) return c.name;
    }
    return "";
}

inline std::string resolve_cpu_library_path(const std::string& cpu_name) {
    if (cpu_name.empty()) return "";
    std::string exe_dir = get_executable_dir();
    std::vector<std::string> candidates = {
        exe_dir + "/data/cpu/" + cpu_name + "/lib" + cpu_name + ".so",
        exe_dir + "/lib" + cpu_name + ".so",
        "./data/cpu/" + cpu_name + "/lib" + cpu_name + ".so",
        "./lib" + cpu_name + ".so"
    };

    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return "";
}

inline bool ensure_toolchain_loaded(AppState& s) {
    const std::string cpu_name = get_active_cpu_name(s);
    if (cpu_name.empty()) {
        g_toolchain.last_error = "No CPU component selected in motherboard config.";
        return false;
    }

    if (g_toolchain.cpu_iface && g_toolchain.cpu_name == cpu_name) return true;

    unload_toolchain();

    std::string so_path = resolve_cpu_library_path(cpu_name);
    if (so_path.empty()) {
        g_toolchain.last_error = "Could not find CPU library for: " + cpu_name;
        return false;
    }

    void* handle = dlopen(so_path.c_str(), RTLD_LAZY);
    if (!handle) {
        g_toolchain.last_error = "dlopen failed: " + std::string(dlerror());
        return false;
    }

    auto get_cpu = reinterpret_cast<GetCPUInterfaceFunc>(dlsym(handle, "get_cpu_interface"));
    if (!get_cpu) {
        g_toolchain.last_error = "Could not resolve get_cpu_interface in " + so_path;
        dlclose(handle);
        return false;
    }

    CPU_Interface* iface = get_cpu();
    if (!iface) {
        g_toolchain.last_error = "CPU interface is null for " + cpu_name;
        dlclose(handle);
        return false;
    }

    if (!iface->get_language_count || !iface->get_language_descriptor ||
        !iface->build_source || !iface->free_binary_output) {
        g_toolchain.last_error = "CPU plugin does not implement toolchain API.";
        dlclose(handle);
        return false;
    }

    g_toolchain.so_handle = handle;
    g_toolchain.cpu_iface = iface;
    g_toolchain.cpu_name = cpu_name;
    g_toolchain.so_path = so_path;
    g_toolchain.last_error.clear();
    g_language_idx = 0;
    return true;
}

static void build_log_callback(int level, const char* message, void* user_data) {
    if (!user_data) return;
    auto* log = static_cast<BuildLog*>(user_data);
    std::string msg = message ? message : "";
    if (level == CPU_BUILD_LOG_ERROR) log->error(msg);
    else if (level == CPU_BUILD_LOG_WARNING) log->warn(msg);
    else log->info(msg);
}

inline void build_to_rom_via_cpu_plugin(AppState& s, int slot_idx) {
    s.build_log.clear();

    if (!ensure_toolchain_loaded(s)) {
        s.build_log.error(g_toolchain.last_error.empty() ? "CPU toolchain unavailable" : g_toolchain.last_error);
        return;
    }

    uint32_t lang_count = g_toolchain.cpu_iface->get_language_count();
    if (lang_count == 0) {
        s.build_log.error("CPU plugin reports no supported source languages.");
        return;
    }
    if (g_language_idx < 0 || static_cast<uint32_t>(g_language_idx) >= lang_count) g_language_idx = 0;

    const CPU_LanguageDescriptor* lang = g_toolchain.cpu_iface->get_language_descriptor(static_cast<uint32_t>(g_language_idx));
    if (!lang || !lang->id) {
        s.build_log.error("Invalid language descriptor from CPU plugin.");
        return;
    }

    auto& slot = s.rom_slots[slot_idx];
    const char* src = slot_bufs[slot_idx].data();

    s.build_log.info("=== Building slot [" + slot.name + "] with CPU [" + g_toolchain.cpu_name + "] ===");
    s.build_log.info(std::string("Language: ") + (lang->display_name ? lang->display_name : lang->id));

    CPU_BinaryOutput out{};
    int ok = g_toolchain.cpu_iface->build_source(lang->id, src, &out, build_log_callback, &s.build_log);
    if (!ok) {
        s.build_log.error("Build FAILED");
        if (g_toolchain.cpu_iface->free_binary_output) g_toolchain.cpu_iface->free_binary_output(&out);
        return;
    }

    size_t slot_size = slot.data.size();
    slot.data.assign(slot_size, 0);
    size_t copy = std::min(static_cast<size_t>(out.size), slot_size);
    if (copy > 0 && out.data) std::copy(out.data, out.data + copy, slot.data.begin());

    slot.modified = true;
    slot.source = RomSlot::Source::Code;
    s.code_buffers[slot_idx] = std::string(src);
    s.code_sources[slot_idx] = RomSlot::Source::Code;

    s.build_log.info("Output bytes: " + std::to_string(out.size) + ", copied to ROM: " + std::to_string(copy));
    s.build_log.info("Origin: 0x" + [] (uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%04X", v);
        return std::string(b);
    }(out.origin));
    s.build_log.info("Build OK");

    g_toolchain.cpu_iface->free_binary_output(&out);
}

// ============================================================

inline void render(AppState& s) {
    ensure_buffers(s);

    if (ImGui::Begin("Code Editor", &s.show_code_panel)) {
        if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
            ImGui::TextDisabled("Select a ROM image to edit its code.");
            ImGui::End(); return;
        }

        int idx = s.active_rom_idx;
        auto& slot = s.rom_slots[idx];

        bool has_toolchain = ensure_toolchain_loaded(s);
        uint32_t lang_count = has_toolchain ? g_toolchain.cpu_iface->get_language_count() : 0;
        if (lang_count == 0) g_language_idx = 0;
        if (g_language_idx < 0 || static_cast<uint32_t>(g_language_idx) >= lang_count) g_language_idx = 0;

        const CPU_LanguageDescriptor* current_lang = nullptr;
        if (has_toolchain && lang_count > 0) {
            current_lang = g_toolchain.cpu_iface->get_language_descriptor(static_cast<uint32_t>(g_language_idx));
        }

        if (!has_toolchain) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "CPU toolchain unavailable: %s", g_toolchain.last_error.c_str());
        }

        // ---- Source language selector (from CPU plugin) ----
        ImGui::SetNextItemWidth(220);
        const char* preview = (current_lang && current_lang->display_name) ? current_lang->display_name : "No language";
        if (ImGui::BeginCombo("Language", preview)) {
            if (has_toolchain) {
                for (uint32_t i = 0; i < lang_count; i++) {
                    const CPU_LanguageDescriptor* lang = g_toolchain.cpu_iface->get_language_descriptor(i);
                    const char* name = (lang && lang->display_name) ? lang->display_name : "Unnamed";
                    bool selected = (static_cast<int>(i) == g_language_idx);
                    if (ImGui::Selectable(name, selected)) g_language_idx = static_cast<int>(i);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();

        // ---- Build button ----
        bool build_pressed = ImGui::Button("Build -> ROM");
        ImGui::SameLine();
        if (slot.source == RomSlot::Source::Code)
            ImGui::TextColored(ImVec4(0.3f,0.9f,0.3f,1), "Last built OK");
        else if (slot.modified)
            ImGui::TextColored(ImVec4(1,0.7f,0,1), "*not built");

        if (current_lang && current_lang->syntax_keywords && current_lang->syntax_keywords[0] != '\0') {
            ImGui::SeparatorText("Syntax Hints (From CPU Plugin)");
            ImGui::TextWrapped("%s", current_lang->syntax_keywords);
        }

        ImGui::Separator();

        // ---- Editor ----
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                       ? ImGui::GetIO().Fonts->Fonts[1]
                       : ImGui::GetIO().Fonts->Fonts[0]);

        ImGui::InputTextMultiline(
            "##code",
            slot_bufs[idx].data(),
            CODE_BUF_SIZE,
            ImVec2(-1, -1),
            ImGuiInputTextFlags_AllowTabInput);

        ImGui::PopFont();

        if (build_pressed) {
            build_to_rom_via_cpu_plugin(s, idx);
        }
    }
    ImGui::End();
}

// ============================================================
// Build output / console

inline void render_build_panel(AppState& s) {
    if (ImGui::Begin("Build Output", &s.show_build_panel)) {
        if (ImGui::SmallButton("Clear")) s.build_log.clear();
        ImGui::Separator();

        std::string build_text;
        for (const auto& e : s.build_log.entries) {
            switch (e.level) {
            case LogEntry::Level::Error:
                build_text += "[ERR] ";
                break;
            case LogEntry::Level::Warning:
                build_text += "[WRN] ";
                break;
            default:
                build_text += "[INF] ";
                break;
            }
            build_text += e.message;
            build_text += '\n';
        }

        std::vector<char> build_buffer(build_text.begin(), build_text.end());
        build_buffer.push_back('\0');

        ImGui::InputTextMultiline(
            "##BuildLog",
            build_buffer.data(),
            build_buffer.size(),
            ImVec2(-FLT_MIN, -FLT_MIN),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput);
    }
    ImGui::End();
}

} // namespace CodePanel
