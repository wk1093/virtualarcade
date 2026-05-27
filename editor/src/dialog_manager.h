#pragma once
#include <string>
#include <filesystem>
#include "app_state.h"

namespace fs = std::filesystem;

class DialogManager {
public:
    DialogManager() = default;

    // Dialog state management
    void request_save_motherboard_dialog() { 
        save_mb_request_ = true;
    }
    void request_save_rom_dialog() { 
        save_rom_request_ = true;
    }
    void request_open_motherboard_dialog() { 
        open_mb_request_ = true;
    }
    void request_open_rom_dialog() { 
        open_rom_request_ = true;
    }

    // Render all dialogs each frame
    void render(AppState& state);

private:
    bool save_mb_request_ = false;
    bool save_rom_request_ = false;
    bool open_mb_request_ = false;
    bool open_rom_request_ = false;

    // Use dynamic buffers instead of fixed-size arrays
    std::string save_mb_path;
    std::string save_rom_path;
    std::string open_mb_path;
    std::string open_rom_path;

    static constexpr int MAX_PATH_LENGTH = 4096;

    // Individual dialog renderers
    void render_save_motherboard_dialog(AppState& state);
    void render_save_rom_dialog(AppState& state);
    void render_open_motherboard_dialog(AppState& state);
    void render_open_rom_dialog(AppState& state);

    bool validate_file_path(const std::string& path);
};
