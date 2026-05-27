add_executable(runner src/main.cpp ../common/src/config.cpp)

target_include_directories(runner PRIVATE ../common/include)
target_link_libraries(runner PRIVATE nlohmann_json::nlohmann_json)

# Depending on your OS, you need to link 'dl' for dynamic loading
if(UNIX)
    target_link_libraries(runner PRIVATE dl)
endif()