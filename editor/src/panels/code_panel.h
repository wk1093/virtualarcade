#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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

struct LogAutoScrollState {
    bool follow_tail = false;
};

inline int readonly_tail_scroll_callback(ImGuiInputTextCallbackData* data) {
    if (!data || !data->UserData) return 0;
    auto* state = static_cast<LogAutoScrollState*>(data->UserData);
    if (!state->follow_tail) return 0;
    data->CursorPos = data->BufTextLen;
    data->SelectionStart = data->BufTextLen;
    data->SelectionEnd = data->BufTextLen;
    return 0;
}

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
static bool g_show_syntax_preview = true;

struct LanguageSyntaxDefinition {
    std::string language_id;
    std::string language_type;
    std::string display_name;
    std::string line_comment;
    bool case_sensitive = false;
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> directives;
    std::unordered_set<std::string> types;
};

struct SyntaxRegistry {
    std::string cpu_name;
    bool loaded = false;
    std::unordered_map<std::string, LanguageSyntaxDefinition> by_id;
    std::unordered_map<std::string, LanguageSyntaxDefinition> by_type;
};

static SyntaxRegistry g_syntax_registry;

inline std::string get_executable_dir();

inline std::string to_upper_ascii(std::string v) {
    for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return v;
}

inline std::string to_lower_ascii(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

inline bool is_number_token(const std::string& tok) {
    if (tok.empty()) return false;
    if (tok.size() > 1 && tok[0] == '$') {
        for (size_t i = 1; i < tok.size(); ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(tok[i]))) return false;
        }
        return true;
    }
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        for (size_t i = 2; i < tok.size(); ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(tok[i]))) return false;
        }
        return true;
    }
    size_t i = (tok[0] == '-' || tok[0] == '+') ? 1 : 0;
    bool has_digit = false;
    for (; i < tok.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(tok[i]))) has_digit = true;
        else return false;
    }
    return has_digit;
}

inline void add_words_to_set(const json& src, std::unordered_set<std::string>& out, bool case_sensitive) {
    auto add_one = [&](const std::string& word) {
        if (word.empty()) return;
        out.insert(case_sensitive ? word : to_upper_ascii(word));
    };

    if (src.is_array()) {
        for (const auto& item : src) {
            if (item.is_string()) add_one(item.get<std::string>());
        }
        return;
    }

    if (src.is_string()) {
        std::istringstream iss(src.get<std::string>());
        std::string tok;
        while (iss >> tok) add_one(tok);
    }
}

inline std::vector<std::string> cpu_data_dir_candidates(const std::string& cpu_name) {
    std::string exe_dir = get_executable_dir();
    return {
        exe_dir + "/data/cpu/" + cpu_name,
        "./data/cpu/" + cpu_name,
        "../data/cpu/" + cpu_name,
        "cpus/" + cpu_name + "/data",
        "../cpus/" + cpu_name + "/data",
        "../../cpus/" + cpu_name + "/data"
    };
}

inline bool read_json_file(const std::string& path, json& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        f >> out;
        return true;
    } catch (...) {
        return false;
    }
}

inline LanguageSyntaxDefinition parse_syntax_json(
    const std::string& syntax_path,
    const std::string& language_id,
    const std::string& language_type,
    const std::string& display_name
) {
    LanguageSyntaxDefinition def;
    def.language_id = language_id;
    def.language_type = language_type;
    def.display_name = display_name;

    json j;
    if (!read_json_file(syntax_path, j)) return def;

    def.case_sensitive = j.value("case_sensitive", false);
    def.line_comment = j.value("line_comment", language_type == "asm" ? ";" : "//");

    add_words_to_set(j.value("keywords", json::array()), def.keywords, def.case_sensitive);
    add_words_to_set(j.value("directives", json::array()), def.directives, def.case_sensitive);
    add_words_to_set(j.value("types", json::array()), def.types, def.case_sensitive);

    return def;
}

inline void ensure_syntax_registry_loaded(const std::string& cpu_name) {
    if (cpu_name.empty()) return;
    if (g_syntax_registry.loaded && g_syntax_registry.cpu_name == cpu_name) return;

    g_syntax_registry = SyntaxRegistry{};
    g_syntax_registry.cpu_name = cpu_name;

    for (const auto& data_dir : cpu_data_dir_candidates(cpu_name)) {
        const std::string cpu_json_path = data_dir + "/cpu.json";
        json cpu_json;
        if (!read_json_file(cpu_json_path, cpu_json)) continue;

        const json languages = cpu_json.value("compilation", json::object()).value("languages", json::array());
        if (!languages.is_array()) break;

        for (const auto& lang : languages) {
            if (!lang.is_object()) continue;

            const std::string language_type = to_lower_ascii(lang.value("type", ""));
            const std::string language_id = lang.value("id", language_type);
            const std::string display_name = lang.value("name", language_id);
            const std::string syntax_file = lang.value("syntax", "");

            if (language_id.empty()) continue;

            LanguageSyntaxDefinition def;
            if (!syntax_file.empty()) {
                const std::string syntax_path = (fs::path(data_dir) / syntax_file).string();
                def = parse_syntax_json(syntax_path, language_id, language_type, display_name);
            }

            if (def.language_id.empty()) {
                def.language_id = language_id;
                def.language_type = language_type;
                def.display_name = display_name;
                def.line_comment = language_type == "asm" ? ";" : "//";
            }

            add_words_to_set(lang.value("keywords", json::array()), def.keywords, def.case_sensitive);
            add_words_to_set(lang.value("directives", json::array()), def.directives, def.case_sensitive);

            g_syntax_registry.by_id[language_id] = def;
            if (!language_type.empty()) {
                g_syntax_registry.by_type[language_type] = def;
            }
        }
        break;
    }

    g_syntax_registry.loaded = true;
}

inline std::string language_type_from_id(const char* language_id) {
    if (!language_id) return "";
    std::string id = to_lower_ascii(language_id);
    size_t dot = id.find('.');
    return (dot == std::string::npos) ? id : id.substr(0, dot);
}

inline ImVec4 syntax_color_for_token(const LanguageSyntaxDefinition& def, const std::string& token) {
    if (token.empty()) return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
    const std::string lookup = def.case_sensitive ? token : to_upper_ascii(token);
    if (def.directives.count(lookup)) return ImVec4(0.97f, 0.65f, 0.30f, 1.0f);
    if (def.keywords.count(lookup)) return ImVec4(0.40f, 0.83f, 1.0f, 1.0f);
    if (def.types.count(lookup)) return ImVec4(0.72f, 0.86f, 0.52f, 1.0f);
    if (is_number_token(token)) return ImVec4(0.90f, 0.79f, 0.42f, 1.0f);
    return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
}

inline void draw_token_colored(const char* begin, const char* end, const ImVec4& color) {
    if (begin >= end) return;
    ImGui::TextColored(color, "%.*s", (int)(end - begin), begin);
    ImGui::SameLine(0.0f, 0.0f);
}

inline ImU32 color_u32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

inline void draw_overlay_token(ImDrawList* draw_list, float& x, float y, const char* begin, const char* end, ImU32 color) {
    if (!draw_list || begin >= end) return;
    draw_list->AddText(ImVec2(x, y), color, begin, end);
    x += ImGui::CalcTextSize(begin, end).x;
}

inline void render_syntax_overlay_line(
    const LanguageSyntaxDefinition& def,
    ImDrawList* draw_list,
    const std::string& line,
    float x_start,
    float y
) {
    const std::string& comment = def.line_comment;
    const char* src = line.c_str();
    size_t n = line.size();
    size_t i = 0;
    float x = x_start;

    while (i < n) {
        if (!comment.empty() && i + comment.size() <= n && line.compare(i, comment.size(), comment) == 0) {
            draw_overlay_token(draw_list, x, y, src + i, src + n, color_u32(ImVec4(0.52f, 0.80f, 0.52f, 1.0f)));
            break;
        }

        char c = line[i];

        if (c == '"' || c == '\'') {
            size_t start = i++;
            while (i < n) {
                if (line[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (i < n && line[i] == c) {
                    ++i;
                    break;
                }
                ++i;
            }
            draw_overlay_token(draw_list, x, y, src + start, src + std::min(i, n), color_u32(ImVec4(0.95f, 0.62f, 0.66f, 1.0f)));
            continue;
        }

        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '+' || c == '-') {
            size_t start = i;
            while (i < n) {
                char t = line[i];
                if (std::isalnum(static_cast<unsigned char>(t)) || t == '_' || t == '.' || t == '$' || t == '+' || t == '-') ++i;
                else break;
            }
            std::string tok = line.substr(start, i - start);
            draw_overlay_token(draw_list, x, y, src + start, src + i, color_u32(syntax_color_for_token(def, tok)));
            continue;
        }

        draw_overlay_token(draw_list, x, y, src + i, src + i + 1, color_u32(ImVec4(0.88f, 0.88f, 0.88f, 1.0f)));
        ++i;
    }
}

inline void render_syntax_overlay(
    const LanguageSyntaxDefinition& def,
    const char* source_text,
    const ImVec2& item_rect_min,
    const ImVec2& item_rect_max,
    const ImVec2& scroll
) {
    if (!source_text) return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float line_h = ImGui::GetTextLineHeight();

    ImVec2 clip_min(item_rect_min.x + style.FramePadding.x, item_rect_min.y + style.FramePadding.y);
    ImVec2 clip_max(item_rect_max.x - style.FramePadding.x, item_rect_max.y - style.FramePadding.y);

    draw_list->PushClipRect(clip_min, clip_max, true);

    std::string src(source_text);
    float y = clip_min.y - scroll.y;
    size_t start = 0;

    while (start <= src.size() && y <= clip_max.y) {
        size_t end = src.find('\n', start);
        std::string line = (end == std::string::npos) ? src.substr(start) : src.substr(start, end - start);

        if (y + line_h >= clip_min.y) {
            render_syntax_overlay_line(def, draw_list, line, clip_min.x - scroll.x, y);
        }

        y += line_h;
        if (end == std::string::npos) break;
        start = end + 1;
    }

    draw_list->PopClipRect();
}

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
    ensure_syntax_registry_loaded(cpu_name);
    return true;
}

inline std::vector<std::string> get_json_language_ids() {
    std::vector<std::string> ids;
    for (const auto& pair : g_syntax_registry.by_id) {
        ids.push_back(pair.first);
    }
    return ids;
}

inline std::string get_language_id_at_index(int idx) {
    const auto ids = get_json_language_ids();
    if (idx < 0 || idx >= (int)ids.size()) return "";
    return ids[idx];
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

    const auto lang_ids = get_json_language_ids();
    if (lang_ids.empty()) {
        s.build_log.error("No languages defined in CPU configuration.");
        return;
    }
    if (g_language_idx < 0 || g_language_idx >= (int)lang_ids.size()) g_language_idx = 0;

    std::string lang_id = get_language_id_at_index(g_language_idx);
    if (lang_id.empty()) {
        s.build_log.error("No language selected.");
        return;
    }

    auto& slot = s.rom_slots[slot_idx];
    const char* src = slot_bufs[slot_idx].data();

    s.build_log.info("=== Building slot [" + slot.name + "] with CPU [" + g_toolchain.cpu_name + "] ===");
    if (g_syntax_registry.by_id.count(lang_id)) {
        s.build_log.info(std::string("Language: ") + g_syntax_registry.by_id[lang_id].display_name);
    } else {
        s.build_log.info(std::string("Language: ") + lang_id);
    }

    CPU_BinaryOutput out{};
    int ok = g_toolchain.cpu_iface->build_source(lang_id.c_str(), src, &out, build_log_callback, &s.build_log);
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
    ensure_syntax_registry_loaded(get_active_cpu_name(s));

    if (ImGui::Begin("Code Editor", &s.show_code_panel)) {
        if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
            ImGui::TextDisabled("Select a ROM image to edit its code.");
            ImGui::End(); return;
        }

        int idx = s.active_rom_idx;
        auto& slot = s.rom_slots[idx];

        bool has_toolchain = ensure_toolchain_loaded(s);
        const auto json_lang_ids = get_json_language_ids();
        int lang_count = (int)json_lang_ids.size();
        if (lang_count == 0) g_language_idx = 0;
        if (g_language_idx < 0 || g_language_idx >= lang_count) g_language_idx = 0;

        std::string current_lang_id;
        if (lang_count > 0) {
            current_lang_id = get_language_id_at_index(g_language_idx);
        }

        if (!has_toolchain) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "CPU toolchain unavailable: %s", g_toolchain.last_error.c_str());
        }

        // ---- Source language selector (from JSON) ----
        ImGui::SetNextItemWidth(220);
        std::string preview_str = "No language";
        if (!current_lang_id.empty() && g_syntax_registry.by_id.count(current_lang_id)) {
            preview_str = g_syntax_registry.by_id[current_lang_id].display_name;
        }
        if (ImGui::BeginCombo("Language", preview_str.c_str())) {
            for (int i = 0; i < lang_count; i++) {
                std::string lang_id = json_lang_ids[i];
                const std::string& name = g_syntax_registry.by_id[lang_id].display_name;
                bool selected = (i == g_language_idx);
                if (ImGui::Selectable(name.c_str(), selected)) g_language_idx = i;
                if (selected) ImGui::SetItemDefaultFocus();
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

        const LanguageSyntaxDefinition* syntax = nullptr;
        if (!current_lang_id.empty() && g_syntax_registry.by_id.count(current_lang_id)) {
            syntax = &g_syntax_registry.by_id[current_lang_id];
        }

        if (syntax) {
            ImGui::Checkbox("Inline Syntax Highlighting", &g_show_syntax_preview);
        }

        ImGui::Separator();

        // ---- Editor ----
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                       ? ImGui::GetIO().Fonts->Fonts[1]
                       : ImGui::GetIO().Fonts->Fonts[0]);

        const bool inline_highlight = (syntax != nullptr) && g_show_syntax_preview;
        if (inline_highlight) {
            // Hide raw text and draw highlighted text over the same input region.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
        }

        ImGui::InputTextMultiline(
            "##code",
            slot_bufs[idx].data(),
            CODE_BUF_SIZE,
            ImVec2(-1, -1),
            ImGuiInputTextFlags_AllowTabInput);

        if (inline_highlight) {
            ImGui::PopStyleColor();

            ImVec2 scroll(0.0f, 0.0f);
            ImGuiID code_id = ImGui::GetID("##code");
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(code_id)) {
                scroll = state->Scroll;
            }

            render_syntax_overlay(*syntax, slot_bufs[idx].data(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), scroll);
        }

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

        static size_t last_build_size = 0;
        LogAutoScrollState build_scroll;
        build_scroll.follow_tail = build_text.size() != last_build_size;
        last_build_size = build_text.size();

        ImGui::InputTextMultiline(
            "##BuildLog",
            build_buffer.data(),
            build_buffer.size(),
            ImVec2(-FLT_MIN, -FLT_MIN),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
            readonly_tail_scroll_callback,
            &build_scroll);
    }
    ImGui::End();
}

inline void render_program_panel(AppState& s) {
    if (ImGui::Begin("Program Output", &s.show_program_panel)) {
        if (ImGui::SmallButton("Clear")) {
            std::ofstream trunc(s.runner_stdout_log_file, std::ios::trunc);
        }
        ImGui::Separator();

        std::string program_text;
        {
            std::ifstream in(s.runner_stdout_log_file);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                program_text = ss.str();
            }
        }

        std::vector<char> program_buffer(program_text.begin(), program_text.end());
        program_buffer.push_back('\0');

        static size_t last_program_size = 0;
        LogAutoScrollState program_scroll;
        program_scroll.follow_tail = program_text.size() != last_program_size;
        last_program_size = program_text.size();

        ImGui::InputTextMultiline(
            "##ProgramOutput",
            program_buffer.data(),
            program_buffer.size(),
            ImVec2(-FLT_MIN, -FLT_MIN),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
            readonly_tail_scroll_callback,
            &program_scroll);
    }
    ImGui::End();
}

} // namespace CodePanel
