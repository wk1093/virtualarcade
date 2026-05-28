include(FetchContent)

FetchContent_Declare(SDL3 
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git 
    GIT_TAG main
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(ImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(ImGui)
add_library(ImGui STATIC 
    ${imgui_SOURCE_DIR}/imgui.cpp 
    ${imgui_SOURCE_DIR}/imgui_draw.cpp 
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp 
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(ImGui PUBLIC 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

add_executable(editor
    src/main.cpp
    src/dialog_manager.cpp
    src/menu_handler.cpp
)
target_include_directories(editor PRIVATE
    ../common/include
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
    src
)
target_link_libraries(editor PRIVATE SDL3::SDL3 ImGui)
add_dependencies(editor generic_cpu generic_gpu)

# Create data directories in build output
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/ram/generic_ram
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/rom/generic_rom
)

# Copy RAM and ROM data
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_SOURCE_DIR}/common/data/ram/generic_ram/ram.json
        ${CMAKE_BINARY_DIR}/bin/data/ram/generic_ram/
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_SOURCE_DIR}/common/data/rom/generic_rom/rom.json
        ${CMAKE_BINARY_DIR}/bin/data/rom/generic_rom/
)
