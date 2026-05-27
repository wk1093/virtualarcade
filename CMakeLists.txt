cmake_minimum_required(VERSION 3.20)
project(VirtualArcadeEngine C CXX)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

# Add nlohmann/json library
FetchContent_Declare(json 
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(json)

include_directories(common/include)

add_subdirectory(cpus/generic_cpu)
add_subdirectory(gpus/generic_gpu)

# The runner and editor depend on the plugins
add_subdirectory(runner)
add_subdirectory(editor)