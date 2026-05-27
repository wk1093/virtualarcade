cmake_minimum_required(VERSION 3.20)
project(GenericGPU)

set(CMAKE_CXX_STANDARD 20)

include_directories(include ../../../common/include)

add_library(generic_gpu SHARED src/gpu_impl.cpp)

# Make it available to the main project
set(GENERIC_GPU_LIBRARY generic_gpu PARENT_SCOPE)
set(GENERIC_GPU_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
