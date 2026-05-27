#pragma once
#include <stdint.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// CPU Specification - describes ISA, compilation, etc.
typedef struct {
    const char* name;                    // e.g., "Generic CPU", "6502", etc.
    const char* architecture;            // e.g., "8-bit", "16-bit"
    const char* isa_version;             // Instruction set version
    
    // Compilation support
    int supports_assembly;               // Can assemble assembly to bytecode?
    int supports_c_compilation;          // Can compile C to bytecode?
    const char* assembly_syntax;         // e.g., "generic", "6502", "z80"
} CPU_Specification;

// GPU Specification
typedef struct {
    const char* name;                    // e.g., "Generic GPU", "2C02", etc.
    int resolution_w;                    // Default width
    int resolution_h;                    // Default height
} GPU_Specification;

// RAM Specification - describes memory characteristics
typedef struct {
    const char* name;                    // e.g., "Generic RAM", "16K SRAM"
    uint32_t size;                       // Default size in bytes
    uint32_t start_address;              // Default start address
    uint32_t concurrent_read_ports;      // Number of concurrent read ports (1 = single-port)
    uint32_t concurrent_write_ports;     // Number of concurrent write ports
    uint32_t access_time_ns;             // Access time in nanoseconds
} RAM_Specification;

// ROM Specification - describes read-only memory characteristics
typedef struct {
    const char* name;                    // e.g., "Generic ROM", "PRG ROM"
    uint32_t size;                       // Default size in bytes
    uint32_t start_address;              // Default start address
    uint32_t access_time_ns;             // Access time in nanoseconds
} ROM_Specification;

// Function signatures for getting specifications
typedef CPU_Specification* (*GetCPUSpecFunc)();
typedef GPU_Specification* (*GetGPUSpecFunc)();
typedef RAM_Specification* (*GetRAMSpecFunc)();
typedef ROM_Specification* (*GetROMSpecFunc)();
