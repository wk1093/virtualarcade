cmake_minimum_required(VERSION 3.20)
project(GenericCPU)

set(CMAKE_CXX_STANDARD 20)

include_directories(include ../../../common/include)

add_library(generic_cpu SHARED src/cpu_impl.cpp src/assembler.cpp)

# Make it available to the main project
set(GENERIC_CPU_LIBRARY generic_cpu PARENT_SCOPE)
set(GENERIC_CPU_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
