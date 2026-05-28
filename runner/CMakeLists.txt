add_executable(runner src/main.cpp ../common/src/config.cpp)
add_dependencies(runner generic_cpu generic_gpu)

target_include_directories(runner PRIVATE ../common/include)
target_link_libraries(runner PRIVATE nlohmann_json::nlohmann_json)

# Depending on your OS, you need to link 'dl' for dynamic loading and 'stdc++fs' for filesystem
if(UNIX)
    target_link_libraries(runner PRIVATE dl stdc++fs)
endif()

# Create data directories in build output
add_custom_command(TARGET runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/ram/generic_ram
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/rom/generic_rom
)

# Copy RAM and ROM data
add_custom_command(TARGET runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_SOURCE_DIR}/common/data/ram/generic_ram/ram.json
        ${CMAKE_BINARY_DIR}/bin/data/ram/generic_ram/
    COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_SOURCE_DIR}/common/data/rom/generic_rom/rom.json
        ${CMAKE_BINARY_DIR}/bin/data/rom/generic_rom/
)