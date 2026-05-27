#include "generic_cpu.h"
#include "arcade_spec.h"
#include "assembler.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <cstdlib>

namespace {

static const CPU_LanguageDescriptor g_languages[] = {
    {
        "asm.generic",
        "Generic ASM",
        ".asm;.s",
        "NOP BRK LDA LDX STA ADC SBC AND ORA EOR CMP INX INY DEX DEY TAX TAY TXA TYA JMP JSR RTS BEQ BNE BCC BCS .ORG .DB .DW .ASCII"
    }
};

} // namespace

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

    uint8_t opcode = memory[pc];

    switch (opcode) {
        case 0xEA: // NOP
            break;
        case 0xA9: // LDA immediate
            pc++;
            a = memory[pc];
            update_zn_flags(a);
            break;
        case 0xA2: // LDX immediate
            pc++;
            x = memory[pc];
            update_zn_flags(x);
            break;
        case 0xAD: { // LDA absolute
            uint16_t addr = static_cast<uint16_t>(memory[pc + 1] | (memory[pc + 2] << 8));
            a = memory[addr];
            update_zn_flags(a);
            pc += 2;
            break;
        }
        case 0xBD: { // LDA absolute,X
            uint16_t addr = static_cast<uint16_t>(memory[pc + 1] | (memory[pc + 2] << 8));
            a = memory[static_cast<uint16_t>(addr + x)];
            update_zn_flags(a);
            pc += 2;
            break;
        }
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
        case 0x00: // BRK
            std::cout << "BRK encountered at PC: 0x" << std::hex << pc << std::dec << std::endl;
            break;
        case 0x4C: { // JMP absolute
            uint16_t addr = memory[pc + 1] | (memory[pc + 2] << 8);
            pc = addr;
            cycle_count++;
            return;
        }
        case 0x20: { // JSR absolute - with stack bounds checking
            // Check stack bounds before pushing
            if (sp < 2) {
                std::cout << "Stack overflow during JSR at PC: 0x" << std::hex << pc << std::dec << std::endl;
                break;
            }
            uint16_t ret_addr = pc + 3 - 1;
            memory[0x100 + sp] = static_cast<uint8_t>((ret_addr >> 8) & 0xFF);
            sp--;
            memory[0x100 + sp] = static_cast<uint8_t>(ret_addr & 0xFF);
            sp--;
            uint16_t addr = memory[pc + 1] | (memory[pc + 2] << 8);
            pc = addr;
            cycle_count++;
            return;
        }
        case 0x60: { // RTS - with stack bounds checking
            // Check stack bounds before popping
            if (sp > 0xFD) {
                std::cout << "Stack underflow during RTS at PC: 0x" << std::hex << pc << std::dec << std::endl;
                break;
            }
            sp++;
            uint16_t ret_addr = memory[0x100 + sp];
            sp++;
            ret_addr |= static_cast<uint16_t>(memory[0x100 + sp] << 8);
            pc = ret_addr;
            break;
        }
        case 0xD0: { // BNE relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (!get_flag(FLAG_ZERO)) pc = static_cast<uint16_t>(pc + 2 + offset);
            else pc += 2;
            cycle_count++;
            return;
        }
        case 0xF0: { // BEQ relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (get_flag(FLAG_ZERO)) pc = static_cast<uint16_t>(pc + 2 + offset);
            else pc += 2;
            cycle_count++;
            return;
        }
        case 0x90: { // BCC relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (!get_flag(FLAG_CARRY)) pc = static_cast<uint16_t>(pc + 2 + offset);
            else pc += 2;
            cycle_count++;
            return;
        }
        case 0xB0: { // BCS relative
            int8_t offset = static_cast<int8_t>(memory[pc + 1]);
            if (get_flag(FLAG_CARRY)) pc = static_cast<uint16_t>(pc + 2 + offset);
            else pc += 2;
            cycle_count++;
            return;
        }
        default:
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

static GenericCPU* g_cpu = nullptr;

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

static uint32_t cpu_get_language_count() {
    return static_cast<uint32_t>(sizeof(g_languages) / sizeof(g_languages[0]));
}

static const CPU_LanguageDescriptor* cpu_get_language_descriptor(uint32_t index) {
    if (index >= cpu_get_language_count()) return nullptr;
    return &g_languages[index];
}

static int cpu_build_source(
    const char* language_id,
    const char* source,
    CPU_BinaryOutput* output,
    CPU_BuildLogFn log_fn,
    void* user_data
) {
    if (!output) return 0;
    output->data = nullptr;
    output->size = 0;
    output->origin = 0;

    if (!language_id || !source) {
        if (log_fn) log_fn(CPU_BUILD_LOG_ERROR, "Missing language or source", user_data);
        return 0;
    }

    std::string lang(language_id);
    if (lang != "asm.generic") {
        if (log_fn) log_fn(CPU_BUILD_LOG_ERROR, "Unsupported language for Generic CPU", user_data);
        return 0;
    }

    // Use the new GenericAssembler class
    AsmResult res = GenericAssembler::assemble(source);
    if (!res.success()) {
        for (const auto& e : res.errors) {
            if (log_fn) {
                std::string msg = "Line " + std::to_string(e.line) + ": " + e.message;
                log_fn(CPU_BUILD_LOG_ERROR, msg.c_str(), user_data);
            }
        }
        return 0;
    }

    if (!res.bytes.empty()) {
        output->data = static_cast<uint8_t*>(std::malloc(res.bytes.size()));
        if (!output->data) {
            if (log_fn) log_fn(CPU_BUILD_LOG_ERROR, "Out of memory allocating build output", user_data);
            return 0;
        }
        std::memcpy(output->data, res.bytes.data(), res.bytes.size());
    }

    output->size = static_cast<uint32_t>(res.bytes.size());
    output->origin = res.origin;

    if (log_fn) {
        std::string msg = "Assembled " + std::to_string(output->size) + " bytes";
        log_fn(CPU_BUILD_LOG_INFO, msg.c_str(), user_data);
    }

    return 1;
}

static void cpu_free_binary_output(CPU_BinaryOutput* output) {
    if (!output) return;
    if (output->data) std::free(output->data);
    output->data = nullptr;
    output->size = 0;
    output->origin = 0;
}

extern "C" {
CPU_Interface* get_cpu_interface() {
    if (!g_cpu) g_cpu = new GenericCPU();

    static CPU_Interface interface = {
        cpu_reset,
        cpu_step,
        cpu_get_pc,
        cpu_get_name,
        cpu_get_language_count,
        cpu_get_language_descriptor,
        cpu_build_source,
        cpu_free_binary_output
    };

    return &interface;
}

CPU_Specification* get_cpu_spec() {
    static CPU_Specification spec = {
        .name = "Generic CPU",
        .architecture = "8-bit processor",
        .isa_version = "1.1",
        .supports_assembly = 1,
        .supports_c_compilation = 0,
        .assembly_syntax = "generic"
    };

    return &spec;
}
}
