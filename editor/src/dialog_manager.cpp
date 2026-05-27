#include "dialog_manager.h"
#include <imgui.h>
#include "panels/motherboard_panel.h"

bool DialogManager::validate_file_path(const std::string& path) {
    return !path.empty() && path.length() <= MAX_PATH_LENGTH;
}

void DialogManager::render(AppState& state) {
    // Handle dialog open requests
    if (save_mb_request_) {
        if (!state.motherboard_file.empty()) {
            save_mb_path = state.motherboard_file;
        }
        ImGui::OpenPopup("SaveMBDialog");
        save_mb_request_ = false;
    }
    if (save_rom_request_) {
        if (!state.image_file.empty()) {
            save_rom_path = state.image_file;
        }
        ImGui::OpenPopup("SaveRomDialog");
        save_rom_request_ = false;
    }
    if (open_mb_request_) {
        ImGui::OpenPopup("OpenMBDialog");
        open_mb_request_ = false;
    }
    if (open_rom_request_) {
        ImGui::OpenPopup("OpenRomDialog");
        open_rom_request_ = false;
    }

    // Render each dialog
    render_save_motherboard_dialog(state);
    render_save_rom_dialog(state);
    render_open_motherboard_dialog(state);
    render_open_rom_dialog(state);
}

void DialogManager::render_save_motherboard_dialog(AppState& state) {
    if (ImGui::BeginPopupModal("SaveMBDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save motherboard config as:");
        ImGui::SetNextItemWidth(400);
        
        // Use std::string with ImGui's string input helper
        char buf[MAX_PATH_LENGTH] = {};
        strncpy(buf, save_mb_path.c_str(), MAX_PATH_LENGTH - 1);
        
        if (ImGui::InputText("##mb_path", buf, MAX_PATH_LENGTH)) {
            save_mb_path = buf;
        }
        
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (validate_file_path(save_mb_path)) {
                if (state.save_motherboard(save_mb_path)) {
                    state.build_log.info("Saved: " + save_mb_path);
                    save_mb_path.clear();
                } else {
                    state.build_log.error("Save failed: " + save_mb_path);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            save_mb_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DialogManager::render_save_rom_dialog(AppState& state) {
    if (ImGui::BeginPopupModal("SaveRomDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save ROM image as:");
        ImGui::SetNextItemWidth(400);
        
        char buf[MAX_PATH_LENGTH] = {};
        strncpy(buf, save_rom_path.c_str(), MAX_PATH_LENGTH - 1);
        
        if (ImGui::InputText("##rom_path", buf, MAX_PATH_LENGTH)) {
            save_rom_path = buf;
        }
        
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (validate_file_path(save_rom_path)) {
                if (state.save_image(save_rom_path)) {
                    state.build_log.info("Saved: " + save_rom_path);
                    save_rom_path.clear();
                } else {
                    state.build_log.error("Save failed: " + save_rom_path);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            save_rom_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DialogManager::render_open_motherboard_dialog(AppState& state) {
    if (ImGui::BeginPopupModal("OpenMBDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Open motherboard config (.json):");
        ImGui::SetNextItemWidth(400);
        
        char buf[MAX_PATH_LENGTH] = {};
        strncpy(buf, open_mb_path.c_str(), MAX_PATH_LENGTH - 1);
        
        if (ImGui::InputText("##open_mb", buf, MAX_PATH_LENGTH)) {
            open_mb_path = buf;
        }
        
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (validate_file_path(open_mb_path)) {
                if (state.load_motherboard(open_mb_path)) {
                    state.build_log.info("Opened: " + open_mb_path);
                    MotherboardPanel::needs_sync = true;
                    open_mb_path.clear();
                } else {
                    state.build_log.error("Failed to open: " + open_mb_path);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            open_mb_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DialogManager::render_open_rom_dialog(AppState& state) {
    if (ImGui::BeginPopupModal("OpenRomDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Open ROM image (.img):");
        ImGui::SetNextItemWidth(400);
        
        char buf[MAX_PATH_LENGTH] = {};
        strncpy(buf, open_rom_path.c_str(), MAX_PATH_LENGTH - 1);
        
        if (ImGui::InputText("##open_rom", buf, MAX_PATH_LENGTH)) {
            open_rom_path = buf;
        }
        
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (validate_file_path(open_rom_path)) {
                if (state.load_image(open_rom_path)) {
                    state.build_log.info("Opened: " + open_rom_path);
                    open_rom_path.clear();
                } else {
                    state.build_log.error("Failed to open: " + open_rom_path);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            open_rom_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
