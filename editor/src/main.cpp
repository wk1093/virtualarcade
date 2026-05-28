#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include "app_state.h"
#include "dialog_manager.h"
#include "menu_handler.h"
#include "panels/motherboard_panel.h"
#include "panels/rom_panel.h"
#include "panels/code_panel.h"

namespace fs = std::filesystem;

// ============================================================
// UI Rendering Helpers
// ============================================================

static void render_left_sidebar(AppState& s) {
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

static void render_content_panes(AppState& s) {
    RomPanel::render_center(s);
    CodePanel::render(s);
    CodePanel::render_build_panel(s);
    CodePanel::render_program_panel(s);
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

    // Create manager instances
    DialogManager dialog_manager;
    MenuHandler menu_handler;

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

        // Render menu bar and check for dialog requests
        if (!menu_handler.render_menu_bar(state, window)) {
            running = false;
        }
        
        // Check for dialog requests and forward them
        if (menu_handler.should_open_save_mb_dialog()) {
            dialog_manager.request_save_motherboard_dialog();
            menu_handler.clear_save_mb_dialog_request();
        }
        if (menu_handler.should_open_save_rom_dialog()) {
            dialog_manager.request_save_rom_dialog();
            menu_handler.clear_save_rom_dialog_request();
        }
        if (menu_handler.should_open_open_mb_dialog()) {
            dialog_manager.request_open_motherboard_dialog();
            menu_handler.clear_open_mb_dialog_request();
        }
        if (menu_handler.should_open_open_rom_dialog()) {
            dialog_manager.request_open_rom_dialog();
            menu_handler.clear_open_rom_dialog_request();
        }

        render_left_sidebar(state);
        render_content_panes(state);
        dialog_manager.render(state);

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
