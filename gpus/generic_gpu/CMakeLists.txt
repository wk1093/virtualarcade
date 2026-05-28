cmake_minimum_required(VERSION 3.20)
project(GenericGPU)

set(CMAKE_CXX_STANDARD 20)

include_directories(include ../../../common/include)

add_library(generic_gpu SHARED src/gpu_impl.cpp)

# Stage GPU plugin directly into runtime data folder.
set_target_properties(generic_gpu PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/data/gpu/generic_gpu
)

# Keep GPU metadata next to the plugin.
add_custom_command(TARGET generic_gpu POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/gpu/generic_gpu
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/data/gpu.json
		${CMAKE_BINARY_DIR}/bin/data/gpu/generic_gpu/
)

# Make it available to the main project
set(GENERIC_GPU_LIBRARY generic_gpu PARENT_SCOPE)
set(GENERIC_GPU_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
