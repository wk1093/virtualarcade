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

    constexpr uint8_t FLAG_CARRY = 1 << 0;
    constexpr uint8_t FLAG_ZERO = 1 << 1;
    constexpr uint8_t FLAG_NEGATIVE = 1 << 7;

    auto set_flag = [&](uint8_t flag, bool enabled) {
        if (enabled) status |= flag;
        else status &= (uint8_t)~flag;
    };

    auto get_flag = [&](uint8_t flag) -> bool {
        return (status & flag) != 0;
    };

    auto update_zn_flags = [&](uint8_t value) {
        set_flag(FLAG_ZERO, value == 0);
        set_flag(FLAG_NEGATIVE, (value & 0x80) != 0);
    };

    // Fetch instruction at PC
    uint8_t opcode = memory[pc];

    // Execute opcodes
    switch (opcode) {
        case 0xEA: // NOP
            break;
        case 0xA9: // LDA immediate
            pc++;
            a = memory[pc];
            update_zn_flags(a);
            break;
        case 0x85: { // STA zero page
            pc++;
            uint8_t addr = memory[pc];
            memory[addr] = a;
            break;
        }
        case 0x69: // ADC immediate
            pc++;
            a += memory[pc];
            update_zn_flags(a);
            break;
        case 0xC9: { // CMP immediate
            pc++;
            uint8_t operand = memory[pc];
            uint8_t result = static_cast<uint8_t>(a - operand);
            set_flag(FLAG_CARRY, a >= operand);
            update_zn_flags(result);
            break;
        }
        case 0xE8: // INX
            x++;
            update_zn_flags(x);
            break;
        case 0xC8: // INY
            y++;
            update_zn_flags(y);
            break;
        case 0x00: // BRK (end execution)
            std::cout << "BRK encountered at PC: 0x" << std::hex << pc << std::endl;
            break;
        case 0x4C: { // JMP absolute
            // Next two bytes form the address (little-endian)
            uint16_t addr = memory[pc + 1] | (memory[pc + 2] << 8);
            pc = addr;
            cycle_count++;
            return;  // Don't increment PC again at end of function
        }
        case 0x20: { // JSR absolute (jump to subroutine)
            // Push return address (PC + 3) onto stack
            uint16_t ret_addr = pc + 3 - 1;  // -1 because we'll increment at end
            memory[0x100 + sp] = (ret_addr >> 8) & 0xFF;
            sp--;
            memory[0x100 + sp] = ret_addr & 0xFF;
            sp--;
            // Jump to subroutine
            uint16_t addr = memory[pc + 1] | (memory[pc + 2] << 8);
            pc = addr;
            cycle_count++;
            return;
        }
        case 0x60: { // RTS (return from subroutine)
            // Pop return address from stack
            sp++;
            uint16_t ret_addr = memory[0x100 + sp];
            sp++;
            ret_addr |= (memory[0x100 + sp] << 8);
            pc = ret_addr;
            break;
        }
        case 0xD0: { // BNE relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (!get_flag(FLAG_ZERO)) {
                pc = static_cast<uint16_t>(pc + 2 + offset);
            } else {
                pc += 2;
            }
            cycle_count++;
            return;
        }
        case 0xF0: { // BEQ relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (get_flag(FLAG_ZERO)) {
                pc = static_cast<uint16_t>(pc + 2 + offset);
            } else {
                pc += 2;
            }
            cycle_count++;
            return;
        }
        case 0x90: { // BCC relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (!get_flag(FLAG_CARRY)) {
                pc = static_cast<uint16_t>(pc + 2 + offset);
            } else {
                pc += 2;
            }
            cycle_count++;
            return;
        }
        case 0xB0: { // BCS relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (get_flag(FLAG_CARRY)) {
                pc = static_cast<uint16_t>(pc + 2 + offset);
            } else {
                pc += 2;
            }
            cycle_count++;
            return;
        }
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
