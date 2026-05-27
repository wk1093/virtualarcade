#pragma once
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "app_state.h"
#include "../assembler.h"

// ============================================================
// ROM Panel
// Left-side lower panel – slot list + centre editor tabs
// The "Add ROM" dialog also lives here.
// ============================================================

namespace RomPanel {

// ---- Add-ROM dialog state ----
static char  new_name_buf[128]  = "ROM_Image";
static int   new_size_kb        = 32;
static int   new_type_idx       = 0;    // 0=ROM 1=RAM

// ---- Import-file dialog state ----
static char  import_path_buf[512] = "";

// ---- Bitmap import state ----
static char  bmp_path_buf[512]    = "";

// ---- Hex editor state ----
static int   selected_byte        = -1;
static int   edit_offset          = -1;
static char  edit_input[10]       = "";
static const int HEX_COLS         = 16;

// ---- ASCII editor state ----
// Large buffer; rebuilt from slot.data on slot change
static char  ascii_edit_buf[1024 * 64] = "";
static int   ascii_active_slot         = -1;

// ---- Code editor state ----
// Handled by CodeEditorPanel; we just trigger assembly here

// ============================================================

inline void render_hex_tab(AppState& s) {
    if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
        ImGui::Text("No ROM slot selected"); return;
    }
    auto& slot = s.rom_slots[s.active_rom_idx];
    if (slot.data.empty()) { ImGui::Text("Empty slot"); return; }

    ImGui::Text("Size: %zu B (0x%zX)", slot.data.size(), slot.data.size());
    ImGui::Separator();

    ImGui::BeginChild("HexView", ImVec2(0, -28), ImGuiChildFlags_Borders);
    for (int row = 0; row * HEX_COLS < (int)slot.data.size(); row++) {
        ImGui::Text("%04X: ", row * HEX_COLS);
        for (int col = 0; col < HEX_COLS; col++) {
            int idx = row * HEX_COLS + col;
            if (idx >= (int)slot.data.size()) break;
            ImGui::SameLine(50 + col * 24);
            char cell[4];
            snprintf(cell, sizeof(cell), "%02X", slot.data[idx]);
            ImGui::PushID(idx);
            if (ImGui::Selectable(cell, selected_byte == idx,
                    ImGuiSelectableFlags_AllowDoubleClick, ImVec2(22, 0))) {
                selected_byte = idx;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    edit_offset = idx;
                    snprintf(edit_input, sizeof(edit_input), "%02X", slot.data[idx]);
                }
            }
            ImGui::PopID();
        }
        // ASCII sidebar
        ImGui::SameLine(50 + HEX_COLS * 24 + 8);
        for (int col = 0; col < HEX_COLS; col++) {
            int idx = row * HEX_COLS + col;
            if (idx >= (int)slot.data.size()) break;
            char ch = (char)slot.data[idx];
            char disp[2] = { (ch >= 0x20 && ch < 0x7F) ? ch : '.', 0 };
            ImGui::Text("%s", disp);
            if (col + 1 < HEX_COLS) ImGui::SameLine(0, 0);
        }
    }
    ImGui::EndChild();

    // Inline edit bar
    if (edit_offset >= 0) {
        ImGui::Text("Edit 0x%04X:", edit_offset);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        if (ImGui::InputText("##hexedit", edit_input, sizeof(edit_input),
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            try {
                uint8_t v = (uint8_t)std::stoul(edit_input, nullptr, 16);
                slot.data[edit_offset] = v;
                slot.modified = true;
            } catch (...) {}
            edit_offset = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) edit_offset = -1;
    }
}

// ============================================================

inline void render_ascii_tab(AppState& s) {
    if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
        ImGui::Text("No ROM slot selected"); return;
    }
    auto& slot = s.rom_slots[s.active_rom_idx];

    // Rebuild buffer when slot changes
    if (ascii_active_slot != s.active_rom_idx) {
        ascii_active_slot = s.active_rom_idx;
        size_t copy = std::min(slot.data.size(), sizeof(ascii_edit_buf) - 1);
        memcpy(ascii_edit_buf, slot.data.data(), copy);
        ascii_edit_buf[copy] = '\0';
    }

    ImGui::Text("Edit as text (non-printable bytes shown as '.')");
    ImGui::Separator();

    // Replace display: non-printable → '.'
    static char display_buf[sizeof(ascii_edit_buf)];
    size_t n = std::min(slot.data.size(), sizeof(display_buf)-1);
    for (size_t i = 0; i < n; i++) {
        uint8_t b = slot.data[i];
        display_buf[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    display_buf[n] = '\0';

    if (ImGui::InputTextMultiline("##ascii", display_buf, sizeof(display_buf),
            ImVec2(-1, -50), ImGuiInputTextFlags_AllowTabInput)) {
        // Write back printable bytes
        size_t len = strlen(display_buf);
        size_t resize_to = len;
        if (resize_to > slot.data.size()) slot.data.resize(resize_to, 0);
        for (size_t i = 0; i < len; i++) {
            slot.data[i] = (uint8_t)display_buf[i];
        }
        slot.modified = true;
        slot.source = RomSlot::Source::Ascii;
    }

    if (ImGui::Button("Embed as ASCII string (fill rest with 0)")) {
        size_t len = strlen(display_buf);
        slot.data.assign(slot.data.size(), 0);
        for (size_t i = 0; i < len && i < slot.data.size(); i++)
            slot.data[i] = (uint8_t)display_buf[i];
        slot.modified = true;
        slot.source = RomSlot::Source::Ascii;
    }
}

// ============================================================

inline void render_bitmap_tab(AppState& s, BuildLog& log) {
    if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
        ImGui::Text("No ROM slot selected"); return;
    }
    auto& slot = s.rom_slots[s.active_rom_idx];

    ImGui::Text("Import a raw binary/bitmap file into the ROM region");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("File Path##bmp", bmp_path_buf, sizeof(bmp_path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        std::ifstream f(bmp_path_buf, std::ios::binary);
        if (!f) {
            log.error(std::string("Cannot open: ") + bmp_path_buf);
        } else {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
            // Fit into slot (truncate or pad with zeros)
            slot.data.assign(slot.data.size(), 0);
            size_t copy = std::min(raw.size(), slot.data.size());
            std::copy(raw.begin(), raw.begin() + copy, slot.data.begin());
            slot.modified = true;
            slot.source   = RomSlot::Source::Bitmap;
            log.info("Imported " + std::to_string(copy) + " bytes from " + std::string(bmp_path_buf));
        }
    }

    ImGui::Spacing();
    ImGui::Text("Data preview (first 256 bytes as palette squares):");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const int SQ = 8, COLS = 32;
    int rows = std::min(256, (int)slot.data.size()) / COLS + 1;
    for (int i = 0; i < std::min(256, (int)slot.data.size()); i++) {
        int row = i / COLS, col = i % COLS;
        uint8_t v = slot.data[i];
        ImVec2 a = { p.x + col * SQ,        p.y + row * SQ };
        ImVec2 b = { p.x + (col+1)*SQ - 1,  p.y + (row+1)*SQ - 1 };
        dl->AddRectFilled(a, b, IM_COL32(v, v, v, 255));
    }
    ImGui::Dummy(ImVec2(COLS * SQ, rows * SQ));
}

// ============================================================

inline void render_import_tab(AppState& s, BuildLog& log) {
    if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
        ImGui::Text("No ROM slot selected"); return;
    }
    auto& slot = s.rom_slots[s.active_rom_idx];

    ImGui::Text("Import raw bytes from any file (e.g., a .rom dump)");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("File Path##imp", import_path_buf, sizeof(import_path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Load File")) {
        std::ifstream f(import_path_buf, std::ios::binary);
        if (!f) {
            log.error(std::string("Cannot open: ") + import_path_buf);
        } else {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
            slot.data = raw;
            slot.modified = true;
            slot.source   = RomSlot::Source::Raw;
            log.info("Loaded " + std::to_string(raw.size()) + " raw bytes from " + std::string(import_path_buf));
        }
    }

    if (!slot.data.empty()) {
        ImGui::Text("Loaded: %zu bytes (0x%zX)", slot.data.size(), slot.data.size());
    }
}

// ============================================================
// ROM Slot manager (left sidebar)

inline void render_slot_list(AppState& s) {
    ImGui::SeparatorText("ROM Images");
    for (int i = 0; i < (int)s.rom_slots.size(); i++) {
        auto& slot = s.rom_slots[i];
        std::string label = (slot.type == VarcadeImage::TYPE_ROM ? "[ROM] " : "[RAM] ")
                          + slot.name + (slot.modified ? " *" : "");
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), s.active_rom_idx == i)) {
            s.active_rom_idx = i;
            ascii_active_slot = -1; // force ASCII buffer rebuild
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add ROM Image")) ImGui::OpenPopup("NewROMDialog");
    ImGui::SameLine();
    if (s.active_rom_idx >= 0 && ImGui::Button("Remove")) {
        s.rom_slots.erase(s.rom_slots.begin() + s.active_rom_idx);
        s.code_buffers.erase(s.code_buffers.begin() + s.active_rom_idx);
        s.code_sources.erase(s.code_sources.begin() + s.active_rom_idx);
        s.active_rom_idx = s.rom_slots.empty() ? -1 : 0;
    }

    // ---- New ROM dialog ----
    if (ImGui::BeginPopupModal("NewROMDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name",    new_name_buf, sizeof(new_name_buf));
        ImGui::DragInt ("Size (KB)", &new_size_kb, 1, 1, 4096);
        ImGui::Combo   ("Type",     &new_type_idx, "ROM\0RAM\0\0");
        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(120,0))) {
            s.add_rom_slot(
                std::string(new_name_buf),
                new_type_idx == 0 ? VarcadeImage::TYPE_ROM : VarcadeImage::TYPE_RAM,
                (uint32_t)new_size_kb * 1024
            );
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ============================================================

inline void render_center(AppState& s) {
    if (ImGui::Begin("ROM Editor", &s.show_rom_panel)) {
        if (s.active_rom_idx < 0 || s.active_rom_idx >= (int)s.rom_slots.size()) {
            ImGui::TextDisabled("Select or create a ROM image from the left panel.");
        } else {
            auto& slot = s.rom_slots[s.active_rom_idx];
            ImGui::Text("Editing: %s  [%zu B]%s",
                slot.name.c_str(), slot.data.size(), slot.modified ? " *" : "");
            ImGui::Separator();

            if (ImGui::BeginTabBar("ROMTabs")) {
                if (ImGui::BeginTabItem("Hex"))    { render_hex_tab(s);               ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("ASCII"))  { render_ascii_tab(s);             ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Bitmap")) { render_bitmap_tab(s, s.build_log); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Import")) { render_import_tab(s, s.build_log); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::End();
}

} // namespace RomPanel
