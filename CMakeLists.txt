cmake_minimum_required(VERSION 3.20)
project(VirtualArcadePlatform C CXX)

set(CMAKE_CXX_STANDARD 20)

include_directories(common/include)

# add_subdirectory(cpus/generic_cpu)
# add_subdirectory(gpus/generic_gpu)

# The runner and editor depend on the plugins
add_subdirectory(runner)
add_subdirectory(editor)