#include "generic_cpu.h"
#include "arcade_spec.h"
#include <iostream>
#include <cstring>

GenericCPU::GenericCPU() : pc(0), a(0), x(0), y(0), sp(0xFF), status(0), cycle_count(0) {}

void GenericCPU::reset() {
    pc = 0x8000;           // Typical reset vector
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFF;
    status = 0;
    cycle_count = 0;
}

void GenericCPU::step(uint8_t* memory) {
    if (!memory) return;

    // Fetch instruction at PC
    uint8_t opcode = memory[pc];

    // Execute a few basic opcodes
    switch (opcode) {
        case 0xEA: // NOP
            break;
        case 0xA9: // LDA immediate
            pc++;
            a = memory[pc];
            break;
        case 0x69: // ADC immediate
            pc++;
            a += memory[pc];
            break;
        case 0xE8: // INX
            x++;
            break;
        case 0xC8: // INY
            y++;
            break;
        case 0x00: // BRK (end execution)
            std::cout << "BRK encountered at PC: 0x" << std::hex << pc << std::endl;
            break;
        default:
            // Unknown opcode - just skip
            break;
    }

    pc++;
    cycle_count++;
}

uint16_t GenericCPU::get_pc() const {
    return pc;
}

const char* GenericCPU::get_name() const {
    return "Generic CPU";
}

// Static instance
static GenericCPU* g_cpu = nullptr;

// C Interface Functions
static void cpu_reset() {
    if (g_cpu) g_cpu->reset();
}

static void cpu_step(uint8_t* memory) {
    if (g_cpu) g_cpu->step(memory);
}

static uint16_t cpu_get_pc() {
    return g_cpu ? g_cpu->get_pc() : 0;
}

static const char* cpu_get_name() {
    return g_cpu ? g_cpu->get_name() : "Unknown";
}

extern "C" {
    CPU_Interface* get_cpu_interface() {
        if (!g_cpu) {
            g_cpu = new GenericCPU();
        }
        
        static CPU_Interface interface = {
            cpu_reset,
            cpu_step,
            cpu_get_pc,
            cpu_get_name
        };
        
        return &interface;
    }

    CPU_Specification* get_cpu_spec() {
        static CPU_Specification spec = {
            .name = "Generic CPU",
            .architecture = "8-bit processor",
            .isa_version = "1.0",
            .supports_assembly = 1,
            .supports_c_compilation = 0,
            .assembly_syntax = "generic"
        };
        
        return &spec;
    }
}
