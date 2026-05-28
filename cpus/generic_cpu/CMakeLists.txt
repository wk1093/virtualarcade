cmake_minimum_required(VERSION 3.20)
project(GenericCPU)

set(CMAKE_CXX_STANDARD 20)

include_directories(include ../../../common/include)

add_library(generic_cpu SHARED src/cpu_impl.cpp src/assembler.cpp)

# Stage CPU plugin directly into runtime data folder.
set_target_properties(generic_cpu PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/data/cpu/generic_cpu
)

# Keep CPU metadata next to the plugin.
add_custom_command(TARGET generic_cpu POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data/cpu/generic_cpu
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/data/cpu.json
		${CMAKE_BINARY_DIR}/bin/data/cpu/generic_cpu/
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/data/lang6502.json
		${CMAKE_BINARY_DIR}/bin/data/cpu/generic_cpu/
)

# Make it available to the main project
set(GENERIC_CPU_LIBRARY generic_cpu PARENT_SCOPE)
set(GENERIC_CPU_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
