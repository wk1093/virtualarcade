#include "menu_handler.h"
#include <imgui.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include "panels/motherboard_panel.h"

std::string MenuHandler::get_runner_path() const {
    char exe_path[4096] = {};
    ssize_t cnt = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (cnt > 0) {
        std::string dir(exe_path, cnt);
        size_t last_slash = dir.find_last_of('/');
        if (last_slash != std::string::npos) {
            return dir.substr(0, last_slash) + "/runner";
        }
    }
    return "./runner";
}

bool MenuHandler::render_menu_bar(AppState& state, void* window) {
    bool running = true;
    
    if (ImGui::BeginMainMenuBar()) {
        render_file_menu(state, running);
        render_view_menu(state);
        render_run_menu(state);
        render_status_indicators(state);
        ImGui::EndMainMenuBar();
    }
    
    return running;
}

void MenuHandler::render_file_menu(AppState& state, bool& running) {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project", "Ctrl+N")) {
            state.new_project();
            MotherboardPanel::needs_sync = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Open Motherboard (.json)...")) {
            request_open_mb_dialog_ = true;
        }
        if (ImGui::MenuItem("Open ROM Image (.img)...")) {
            request_open_rom_dialog_ = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Save Motherboard", "Ctrl+M")) {
            if (!state.motherboard_file.empty()) {
                state.save_motherboard(state.motherboard_file);
            } else {
                request_save_mb_dialog_ = true;
            }
        }
        if (ImGui::MenuItem("Save Motherboard As...")) {
            request_save_mb_dialog_ = true;
        }

        if (ImGui::MenuItem("Save ROM Image", "Ctrl+S")) {
            if (!state.image_file.empty()) {
                if (state.save_image(state.image_file)) {
                    state.build_log.info("Saved ROM image to " + state.image_file);
                } else {
                    state.build_log.error("Failed to save ROM image");
                }
            } else {
                request_save_rom_dialog_ = true;
            }
        }
        if (ImGui::MenuItem("Save ROM Image As...")) {
            request_save_rom_dialog_ = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            running = false;
        }

        ImGui::EndMenu();
    }
}

void MenuHandler::render_view_menu(AppState& state) {
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Motherboard", nullptr, &state.show_motherboard_panel);
        ImGui::MenuItem("ROM Editor",  nullptr, &state.show_rom_panel);
        ImGui::MenuItem("Code Editor", nullptr, &state.show_code_panel);
        ImGui::MenuItem("Build Output", nullptr, &state.show_build_panel);
        ImGui::EndMenu();
    }
}

void MenuHandler::render_run_menu(AppState& state) {
    if (ImGui::BeginMenu("Run")) {
        if (ImGui::MenuItem("Launch Runner with current config")) {
            // Save if needed
            std::string mb = state.motherboard_file.empty() ? "/tmp/vae_tmp_mb.json" : state.motherboard_file;
            std::string img = state.image_file.empty() ? "/tmp/vae_tmp_rom.img" : state.image_file;

            if (!state.save_motherboard(mb)) {
                state.build_log.error("Failed to save motherboard config");
                ImGui::EndMenu();
                return;
            }

            if (!state.save_image(img)) {
                state.build_log.error("Failed to save ROM image");
                ImGui::EndMenu();
                return;
            }

            // Build and execute command
            std::string runner_path = get_runner_path();
            std::string cmd = runner_path + " \"" + mb + "\" \"" + img + "\" &";
            state.build_log.info("Launching: " + cmd);
            
            int result = std::system(cmd.c_str());
            if (result != 0) {
                state.build_log.error("Failed to launch runner (exit code: " + std::to_string(result) + ")");
            } else {
                state.build_log.info("Runner launched successfully");
            }
        }
        ImGui::EndMenu();
    }
}

void MenuHandler::render_status_indicators(AppState& state) {
    if (state.mb_modified) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "  MB*");
    }
    
    bool any_rom_dirty = false;
    for (const auto& r : state.rom_slots) {
        if (r.modified) {
            any_rom_dirty = true;
            break;
        }
    }
    
    if (any_rom_dirty) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "  ROM*");
    }
}
