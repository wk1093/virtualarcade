#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <fstream>
#include <cstdio>
#include "app_state.h"
#include "../assembler.h"

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

// Kick off assembler → ROM slot
inline void assemble_to_rom(AppState& s, int slot_idx) {
    s.build_log.clear();
    s.build_log.info("=== Assembling slot [" + s.rom_slots[slot_idx].name + "] ===");

    const char* src = slot_bufs[slot_idx].data();
    Assembler::AsmResult res = Assembler::assemble(std::string(src));

    if (!res.errors.empty()) {
        for (const auto& e : res.errors)
            s.build_log.error("Line " + std::to_string(e.line) + ": " + e.message);
        s.build_log.error("Assembly FAILED");
    } else {
        auto& slot = s.rom_slots[slot_idx];
        // Pad/truncate output to slot size
        size_t slot_size = slot.data.size();
        slot.data.assign(slot_size, 0);
        size_t copy = std::min(res.bytes.size(), slot_size);
        std::copy(res.bytes.begin(), res.bytes.begin() + copy, slot.data.begin());
        slot.modified = true;
        slot.source   = RomSlot::Source::Code;
        s.code_buffers[slot_idx] = std::string(src);
        s.code_sources[slot_idx] = RomSlot::Source::Code;
        s.build_log.info("Assembled " + std::to_string(res.bytes.size()) + " bytes");
        s.build_log.info("Written to ROM at base 0x"
            + []{ char b[16]; snprintf(b,16,"%04X",0); return std::string(b); }()
            + "  (origin $" + std::to_string(res.origin) + ")");
        s.build_log.info("Assembly OK");
    }
}

// Invoke system C compiler → binary → ROM slot
inline void compile_c_to_rom(AppState& s, int slot_idx) {
    s.build_log.clear();
    s.build_log.info("=== Compiling C for slot [" + s.rom_slots[slot_idx].name + "] ===");

    // Write source to a temp file
    std::string src_path = "/tmp/vae_c_src.c";
    std::string bin_path = "/tmp/vae_c_out.bin";
    {
        std::ofstream f(src_path);
        if (!f) { s.build_log.error("Cannot write temp file"); return; }
        f << slot_bufs[slot_idx].data();
    }

    // Compile to a raw binary using the host cc
    // -ffreestanding: no stdlib assumptions  -Os: size optimise  -nostdlib
    std::string cmd = "cc -ffreestanding -nostdlib -Os -o " + bin_path +
                      " " + src_path + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { s.build_log.error("popen failed"); return; }
    char cbuf[256];
    while (fgets(cbuf, sizeof(cbuf), pipe)) {
        std::string line = cbuf;
        if (!line.empty() && line.back() == '\n') line.pop_back();
        // Heuristic: contains "error:" → error, "warning:" → warn
        if (line.find("error:") != std::string::npos)       s.build_log.error(line);
        else if (line.find("warning:") != std::string::npos) s.build_log.warn(line);
        else                                                  s.build_log.info(line);
    }
    int rc = pclose(pipe);

    if (rc != 0) {
        s.build_log.error("Compilation FAILED (exit " + std::to_string(rc) + ")");
        return;
    }

    // Extract raw bytes with objcopy
    std::string raw_path = "/tmp/vae_c_out.raw";
    std::string objcopy_cmd = "objcopy -O binary " + bin_path + " " + raw_path + " 2>&1";
    pipe = popen(objcopy_cmd.c_str(), "r");
    if (pipe) {
        while (fgets(cbuf, sizeof(cbuf), pipe)) s.build_log.info(cbuf);
        pclose(pipe);
    }

    std::ifstream raw_f(raw_path, std::ios::binary);
    if (!raw_f) {
        s.build_log.error("No binary output produced");
        return;
    }
    std::vector<uint8_t> raw_bytes((std::istreambuf_iterator<char>(raw_f)),
                                     std::istreambuf_iterator<char>());

    auto& slot = s.rom_slots[slot_idx];
    size_t copy = std::min(raw_bytes.size(), slot.data.size());
    slot.data.assign(slot.data.size(), 0);
    std::copy(raw_bytes.begin(), raw_bytes.begin() + copy, slot.data.begin());
    slot.modified = true;
    slot.source   = RomSlot::Source::Code;
    s.code_buffers[slot_idx] = std::string(slot_bufs[slot_idx].data());
    s.build_log.info("Compiled " + std::to_string(copy) + " bytes written to ROM");
    s.build_log.info("Compilation OK");
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

        // ---- Source language selector ----
        static int lang_idx = 0;   // 0 = Generic ASM  1 = C
        ImGui::SetNextItemWidth(120);
        ImGui::Combo("Language", &lang_idx, "Generic ASM\0C\0\0");
        ImGui::SameLine();

        // ---- Build button ----
        bool build_pressed = ImGui::Button(lang_idx == 0 ? "Assemble → ROM" : "Compile → ROM");
        ImGui::SameLine();
        if (slot.source == RomSlot::Source::Code)
            ImGui::TextColored(ImVec4(0.3f,0.9f,0.3f,1), "Last built OK");
        else if (slot.modified)
            ImGui::TextColored(ImVec4(1,0.7f,0,1), "*not built");

        ImGui::Separator();

        // ---- Editor ----
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 
                       ? ImGui::GetIO().Fonts->Fonts[1]  // monospace if loaded
                       : ImGui::GetIO().Fonts->Fonts[0]);

        ImGui::InputTextMultiline(
            "##code",
            slot_bufs[idx].data(),
            CODE_BUF_SIZE,
            ImVec2(-1, -1),
            ImGuiInputTextFlags_AllowTabInput);

        ImGui::PopFont();

        if (build_pressed) {
            if (lang_idx == 0) assemble_to_rom(s, idx);
            else               compile_c_to_rom(s, idx);
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

        ImGui::BeginChild("BuildLog", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& e : s.build_log.entries) {
            switch (e.level) {
            case LogEntry::Level::Error:
                ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1), "[ERR] %s", e.message.c_str());
                break;
            case LogEntry::Level::Warning:
                ImGui::TextColored(ImVec4(1.0f,0.8f,0.0f,1), "[WRN] %s", e.message.c_str());
                break;
            default:
                ImGui::TextUnformatted(e.message.c_str());
                break;
            }
        }
        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace CodePanel
