#pragma once
#include <string>
#include "app_state.h"

class MenuHandler {
public:
    MenuHandler() = default;

    // Render menu bar and return false if application should exit
    bool render_menu_bar(AppState& state, void* window);

    // Get dialog requests - dialogue manager checks these
    bool should_open_save_mb_dialog() const { return request_save_mb_dialog_; }
    bool should_open_save_rom_dialog() const { return request_save_rom_dialog_; }
    bool should_open_open_mb_dialog() const { return request_open_mb_dialog_; }
    bool should_open_open_rom_dialog() const { return request_open_rom_dialog_; }

    // Dialog request clearers (called by dialog manager after handling)
    void clear_save_mb_dialog_request() { request_save_mb_dialog_ = false; }
    void clear_save_rom_dialog_request() { request_save_rom_dialog_ = false; }
    void clear_open_mb_dialog_request() { request_open_mb_dialog_ = false; }
    void clear_open_rom_dialog_request() { request_open_rom_dialog_ = false; }

private:
    bool request_save_mb_dialog_ = false;
    bool request_save_rom_dialog_ = false;
    bool request_open_mb_dialog_ = false;
    bool request_open_rom_dialog_ = false;

    void render_file_menu(AppState& state, bool& running);
    void render_view_menu(AppState& state);
    void render_run_menu(AppState& state);
    void render_status_indicators(AppState& state);
    std::string get_runner_path() const;
};
