#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include "app_state.h"
#include "panels/motherboard_panel.h"
#include "panels/rom_panel.h"
#include "panels/code_panel.h"
#ifdef _WIN32
#error Windows is not supported (yet)
#else
#include <unistd.h>
#endif

// ============================================================
// Helpers
// ============================================================

namespace fs = std::filesystem;

static char g_save_mb_buf[512]  = "";
static char g_save_img_buf[512] = "";
static char g_open_mb_buf[512]  = "";
static char g_open_img_buf[512] = "";

static bool g_request_open_save_mb_dialog = false;
static bool g_request_open_save_img_dialog = false;
static bool g_request_open_open_mb_dialog = false;
static bool g_request_open_open_img_dialog = false;

static void render_menu_bar(AppState& s, SDL_Window* window, bool& running) {
    if (ImGui::BeginMainMenuBar()) {

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                s.new_project();
                MotherboardPanel::needs_sync = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Open Motherboard (.json)..."))
                g_request_open_open_mb_dialog = true;
            if (ImGui::MenuItem("Open ROM Image (.img)..."))
                g_request_open_open_img_dialog = true;

            ImGui::Separator();
            if (ImGui::MenuItem("Save Motherboard", "Ctrl+M")) {
                if (!s.motherboard_file.empty()) s.save_motherboard(s.motherboard_file);
                else g_request_open_save_mb_dialog = true;
            }
            if (ImGui::MenuItem("Save Motherboard As..."))
                g_request_open_save_mb_dialog = true;

            if (ImGui::MenuItem("Save ROM Image", "Ctrl+S")) {
                if (!s.image_file.empty()) {
                    if (s.save_image(s.image_file))
                        s.build_log.info("Saved ROM image to " + s.image_file);
                    else
                        s.build_log.error("Failed to save ROM image");
                } else {
                    g_request_open_save_img_dialog = true;
                }
            }
            if (ImGui::MenuItem("Save ROM Image As..."))
                g_request_open_save_img_dialog = true;

            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Motherboard", nullptr, &s.show_motherboard_panel);
            ImGui::MenuItem("ROM Editor",  nullptr, &s.show_rom_panel);
            ImGui::MenuItem("Code Editor", nullptr, &s.show_code_panel);
            ImGui::MenuItem("Build Output",nullptr, &s.show_build_panel);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Run")) {
            if (ImGui::MenuItem("Launch Runner with current config")) {
                // Save if needed
                std::string mb  = s.motherboard_file.empty() ? "/tmp/vae_tmp_mb.json"  : s.motherboard_file;
                std::string img = s.image_file.empty()       ? "/tmp/vae_tmp_rom.img"  : s.image_file;
                
                if (!s.save_motherboard(mb)) {
                    s.build_log.error("Failed to save motherboard config");
                    ImGui::EndMenu();
                    if (ImGui::IsItemHovered()) {} ImGui::EndPopup(); // graceful exit
                    return;
                }
                
                if (!s.save_image(img)) {
                    s.build_log.error("Failed to save ROM image");
                    ImGui::EndMenu();
                    if (ImGui::IsItemHovered()) {} ImGui::EndPopup(); // graceful exit
                    return;
                }

                // Get runner path relative to this executable
                char exe_path[4096] = {};
                ssize_t cnt = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
                std::string runner_path = "./runner";
                if (cnt > 0) {
                    std::string dir = std::string(exe_path, cnt);
                    dir = dir.substr(0, dir.find_last_of('/'));
                    runner_path = dir + "/runner";
                }
                // Build command with both motherboard config and ROM image
                std::string cmd = runner_path + " \"" + mb + "\" \"" + img + "\" &";
                s.build_log.info("Launching: " + cmd);
                int result = std::system(cmd.c_str());
                if (result != 0) {
                    s.build_log.error("Failed to launch runner (exit code: " + std::to_string(result) + ")");
                } else {
                    s.build_log.info("Runner launched successfully");
                }
            }
            ImGui::EndMenu();
        }

        // ---- Status in menu bar ----
        if (s.mb_modified)
            ImGui::TextColored(ImVec4(1,0.6f,0,1), "  MB*");
        bool any_rom_dirty = false;
        for (auto& r : s.rom_slots) if (r.modified) { any_rom_dirty = true; break; }
        if (any_rom_dirty)
            ImGui::TextColored(ImVec4(1,0.6f,0,1), "  ROM*");

        ImGui::EndMainMenuBar();
    }
}

// ============================================================
// Render all file dialogs (called each frame from main loop)
// ============================================================

static void render_dialogs(AppState& s) {
    if (g_request_open_save_mb_dialog) {
        if (!s.motherboard_file.empty()) {
            std::snprintf(g_save_mb_buf, sizeof(g_save_mb_buf), "%s", s.motherboard_file.c_str());
        }
        ImGui::OpenPopup("SaveMBDialog");
        g_request_open_save_mb_dialog = false;
    }
    if (g_request_open_save_img_dialog) {
        if (!s.image_file.empty()) {
            std::snprintf(g_save_img_buf, sizeof(g_save_img_buf), "%s", s.image_file.c_str());
        }
        ImGui::OpenPopup("SaveImgDialog");
        g_request_open_save_img_dialog = false;
    }
    if (g_request_open_open_mb_dialog) {
        ImGui::OpenPopup("OpenMBDialog");
        g_request_open_open_mb_dialog = false;
    }
    if (g_request_open_open_img_dialog) {
        ImGui::OpenPopup("OpenImgDialog");
        g_request_open_open_img_dialog = false;
    }

    // ---- Save Motherboard Dialog ----
    if (ImGui::BeginPopupModal("SaveMBDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save motherboard config as:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##mb_path", g_save_mb_buf, sizeof(g_save_mb_buf));
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (std::string(g_save_mb_buf)[0] != '\0') {
                if (s.save_motherboard(std::string(g_save_mb_buf))) {
                    s.build_log.info("Saved: " + std::string(g_save_mb_buf));
                    g_save_mb_buf[0] = '\0';
                } else {
                    s.build_log.error("Save failed: " + std::string(g_save_mb_buf));
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_save_mb_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- Save ROM Image Dialog ----
    if (ImGui::BeginPopupModal("SaveImgDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save ROM image as:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##img_path", g_save_img_buf, sizeof(g_save_img_buf));
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (std::string(g_save_img_buf)[0] != '\0') {
                if (s.save_image(std::string(g_save_img_buf))) {
                    s.build_log.info("Saved: " + std::string(g_save_img_buf));
                    g_save_img_buf[0] = '\0';
                } else {
                    s.build_log.error("Save failed: " + std::string(g_save_img_buf));
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_save_img_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- Open Motherboard Dialog ----
    if (ImGui::BeginPopupModal("OpenMBDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Open motherboard config (.json):");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##open_mb", g_open_mb_buf, sizeof(g_open_mb_buf));
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (std::string(g_open_mb_buf)[0] != '\0') {
                if (s.load_motherboard(std::string(g_open_mb_buf))) {
                    s.build_log.info("Opened: " + std::string(g_open_mb_buf));
                    MotherboardPanel::needs_sync = true;
                    g_open_mb_buf[0] = '\0';
                } else {
                    s.build_log.error("Failed to open: " + std::string(g_open_mb_buf));
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_open_mb_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- Open ROM Image Dialog ----
    if (ImGui::BeginPopupModal("OpenImgDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Open ROM image (.img):");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##open_img", g_open_img_buf, sizeof(g_open_img_buf));
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (std::string(g_open_img_buf)[0] != '\0') {
                if (s.load_image(std::string(g_open_img_buf))) {
                    s.build_log.info("Opened: " + std::string(g_open_img_buf));
                    g_open_img_buf[0] = '\0';
                } else {
                    s.build_log.error("Failed to open: " + std::string(g_open_img_buf));
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_open_img_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ============================================================
// Left sidebar: motherboard + rom slot list together
// ============================================================

static void render_left_sidebar(AppState& s) {
    // Bind visibility to show_motherboard_panel (for now use it for whole project panel)
    if (!s.show_motherboard_panel && !s.show_rom_panel) return;
    
    if (ImGui::Begin("Project", nullptr)) {
        if (ImGui::BeginTabBar("SidebarTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Motherboard")) {
                MotherboardPanel::render(s);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ROM Slots")) {
                RomPanel::render_slot_list(s);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ============================================================
// Right area: ROM editor, code editor, build panel
// ============================================================

static void render_content_panes(AppState& s) {
    RomPanel::render_center(s);
    CodePanel::render(s);
    CodePanel::render_build_panel(s);
}

// ============================================================
// Entry point
// ============================================================

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow(
        "VAE – Virtual Arcade Engine Editor",
        1400, 900, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_SetWindowMinimumSize(window, 900, 600);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ImGui::StyleColorsDark();
    // Slightly friendlier palette
    auto& style = ImGui::GetStyle();
    style.WindowRounding   = 4.0f;
    style.FrameRounding    = 3.0f;
    style.ScrollbarRounding= 3.0f;
    style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.36f, 0.56f, 1.0f);
    style.Colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.40f, 0.60f, 0.6f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.52f, 0.78f, 0.8f);

    AppState state;
    // Seed with a basic generic system
    state.motherboard.name = "New System";
    state.motherboard.master_clock = 21477272;
    state.build_log.info("Welcome to VAE Editor");
    state.build_log.info("Open or create a motherboard config and ROM images to get started.");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                if (ctrl && event.key.key == SDLK_S) {
                    if (!state.image_file.empty()) state.save_image(state.image_file);
                }
                if (ctrl && event.key.key == SDLK_M) {
                    if (!state.motherboard_file.empty()) state.save_motherboard(state.motherboard_file);
                }
                if (ctrl && event.key.key == SDLK_N) {
                    state.new_project();
                    MotherboardPanel::needs_sync = true;
                }
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Set up docking space
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        render_menu_bar(state, window, running);
        render_left_sidebar(state);
        render_content_panes(state);
        render_dialogs(state);

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 18, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
