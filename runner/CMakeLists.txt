add_executable(runner src/main.cpp)

# Depending on your OS, you need to link 'dl' for dynamic loading
if(UNIX)
    target_link_libraries(runner PRIVATE dl)
endif()