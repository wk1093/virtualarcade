#include "generic_cpu.h"
#include "arcade_spec.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

struct AsmError {
    int line;
    std::string message;
};

struct AsmResult {
    std::vector<uint8_t> bytes;
    uint32_t origin = 0x8000;
    std::vector<AsmError> errors;
    bool success() const { return errors.empty(); }
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

static bool parse_number(const std::string& tok, uint32_t& out) {
    if (tok.empty()) return false;
    try {
        if (tok[0] == '$')          { out = std::stoul(tok.substr(1), nullptr, 16); return true; }
        if (tok.size() > 2 && tok.substr(0, 2) == "0x") {
            out = std::stoul(tok.substr(2), nullptr, 16);
            return true;
        }
        out = std::stoul(tok, nullptr, 10);
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quote = false;
    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            cur += c;
        } else if ((c == ' ' || c == '\t' || c == ',') && !in_quote) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static AsmResult assemble_generic(const std::string& source) {
    AsmResult result;

    struct PatchSite {
        size_t byte_offset;
        std::string label;
        bool is_relative;
        int line;
        int pc_after;
    };

    std::vector<std::string> lines;
    {
        std::istringstream ss(source);
        std::string line;
        while (std::getline(ss, line)) lines.push_back(line);
    }

    std::unordered_map<std::string, uint32_t> labels;
    std::vector<PatchSite> patches;
    uint32_t pc = result.origin;

    auto emit = [&](uint8_t b) { result.bytes.push_back(b); pc++; };
    auto emit16 = [&](uint16_t w) { emit(static_cast<uint8_t>(w & 0xFF)); emit(static_cast<uint8_t>((w >> 8) & 0xFF)); };
    auto err = [&](int ln, const std::string& msg) { result.errors.push_back({ln, msg}); };

    for (int ln = 0; ln < static_cast<int>(lines.size()); ln++) {
        std::string line = lines[ln];

        auto cpos = line.find(';');
        if (cpos != std::string::npos) line = line.substr(0, cpos);
        line = trim(line);
        if (line.empty()) continue;

        if (line.back() == ':') {
            std::string lname = trim(line.substr(0, line.size() - 1));
            if (!lname.empty()) labels[lname] = pc;
            continue;
        }

        auto space_pos = line.find(' ');
        if (space_pos != std::string::npos && line.find(':') != std::string::npos && line.find(':') < space_pos) {
            std::string lname = trim(line.substr(0, line.find(':')));
            labels[lname] = pc;
            line = trim(line.substr(line.find(':') + 1));
            if (line.empty()) continue;
        }

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;
        std::string mnem = to_upper(tokens[0]);

        if (mnem == ".ORG") {
            uint32_t addr = 0;
            if (tokens.size() < 2 || !parse_number(tokens[1], addr)) {
                err(ln + 1, ".org requires an address");
            } else {
                result.origin = addr;
                pc = addr;
                result.bytes.clear();
            }
        } else if (mnem == ".DB" || mnem == ".BYTE") {
            for (size_t i = 1; i < tokens.size(); i++) {
                uint32_t v = 0;
                if (!parse_number(tokens[i], v)) err(ln + 1, "Invalid byte value: " + tokens[i]);
                else emit(static_cast<uint8_t>(v));
            }
        } else if (mnem == ".DW" || mnem == ".WORD") {
            for (size_t i = 1; i < tokens.size(); i++) {
                uint32_t v = 0;
                if (!parse_number(tokens[i], v)) err(ln + 1, "Invalid word value: " + tokens[i]);
                else emit16(static_cast<uint16_t>(v));
            }
        } else if (mnem == ".ASCII") {
            if (tokens.size() < 2) {
                err(ln + 1, ".ascii needs a quoted string");
                continue;
            }
            std::string s = tokens[1];
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
            for (char c : s) emit(static_cast<uint8_t>(c));
        } else if (mnem == "NOP") emit(0xEA);
        else if (mnem == "BRK") emit(0x00);
        else if (mnem == "INX") emit(0xE8);
        else if (mnem == "INY") emit(0xC8);
        else if (mnem == "DEX") emit(0xCA);
        else if (mnem == "DEY") emit(0x88);
        else if (mnem == "TAX") emit(0xAA);
        else if (mnem == "TAY") emit(0xA8);
        else if (mnem == "TXA") emit(0x8A);
        else if (mnem == "TYA") emit(0x98);
        else if (mnem == "RTS") emit(0x60);
        else if (mnem == "LDA" || mnem == "ADC" || mnem == "SBC" ||
                 mnem == "AND" || mnem == "ORA" || mnem == "EOR" || mnem == "CMP") {
            if (tokens.size() < 2) {
                err(ln + 1, mnem + " needs operand");
                continue;
            }
            std::string op = tokens[1];
            uint8_t opc = 0;
            if (mnem == "LDA") opc = 0xA9;
            else if (mnem == "ADC") opc = 0x69;
            else if (mnem == "SBC") opc = 0xE9;
            else if (mnem == "AND") opc = 0x29;
            else if (mnem == "ORA") opc = 0x09;
            else if (mnem == "EOR") opc = 0x49;
            else if (mnem == "CMP") opc = 0xC9;

            if (op.empty() || op[0] != '#') {
                err(ln + 1, mnem + " currently only supports immediate mode (#value)");
                continue;
            }
            uint32_t v = 0;
            if (!parse_number(op.substr(1), v)) err(ln + 1, "Invalid immediate: " + op);
            else { emit(opc); emit(static_cast<uint8_t>(v)); }
        } else if (mnem == "STA") {
            if (tokens.size() < 2) {
                err(ln + 1, "STA needs operand");
                continue;
            }
            uint32_t v = 0;
            if (!parse_number(tokens[1], v)) err(ln + 1, "Invalid STA address: " + tokens[1]);
            else { emit(0x85); emit(static_cast<uint8_t>(v)); }
        } else if (mnem == "JMP" || mnem == "JSR") {
            if (tokens.size() < 2) {
                err(ln + 1, mnem + " needs address");
                continue;
            }
            emit(mnem == "JMP" ? 0x4C : 0x20);
            uint32_t v = 0;
            if (parse_number(tokens[1], v)) {
                emit16(static_cast<uint16_t>(v));
            } else {
                patches.push_back({result.bytes.size(), tokens[1], false, ln + 1, 0});
                emit16(0);
            }
        } else if (mnem == "BEQ" || mnem == "BNE" || mnem == "BCC" || mnem == "BCS") {
            if (tokens.size() < 2) {
                err(ln + 1, mnem + " needs label/offset");
                continue;
            }
            uint8_t opc = 0;
            if (mnem == "BEQ") opc = 0xF0;
            else if (mnem == "BNE") opc = 0xD0;
            else if (mnem == "BCC") opc = 0x90;
            else if (mnem == "BCS") opc = 0xB0;
            emit(opc);

            uint32_t v = 0;
            if (parse_number(tokens[1], v)) {
                emit(static_cast<uint8_t>(v));
            } else {
                int pc_after = static_cast<int>(pc + 1);
                patches.push_back({result.bytes.size(), tokens[1], true, ln + 1, pc_after});
                emit(0);
            }
        } else {
            err(ln + 1, "Unknown mnemonic: " + mnem);
        }
    }

    for (const auto& p : patches) {
        auto it = labels.find(p.label);
        if (it == labels.end()) {
            result.errors.push_back({p.line, "Undefined label: " + p.label});
            continue;
        }
        uint32_t target = it->second;
        if (p.is_relative) {
            int32_t offset = static_cast<int32_t>(target) - p.pc_after;
            if (offset < -128 || offset > 127) {
                result.errors.push_back({p.line, "Branch out of range to label: " + p.label});
                continue;
            }
            result.bytes[p.byte_offset] = static_cast<uint8_t>(offset & 0xFF);
        } else {
            result.bytes[p.byte_offset] = static_cast<uint8_t>(target & 0xFF);
            result.bytes[p.byte_offset + 1] = static_cast<uint8_t>((target >> 8) & 0xFF);
        }
    }

    return result;
}

static const CPU_LanguageDescriptor g_languages[] = {
    {
        "asm.generic",
        "Generic ASM",
        ".asm;.s",
        "NOP BRK LDA STA ADC SBC AND ORA EOR CMP INX INY DEX DEY TAX TAY TXA TYA JMP JSR RTS BEQ BNE BCC BCS .ORG .DB .DW .ASCII"
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
        case 0x20: { // JSR absolute
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
        case 0x60: { // RTS
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

    AsmResult res = assemble_generic(source);
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
