#pragma once
// Generic CPU Assembler
// Supports the opcode set defined in cpus/generic_cpu/src/cpu_impl.cpp
// Syntax inspired by 6502/generic assembly
//
// Directives:  .org <addr>    set origin
//              .db  <b,...>   define bytes (hex or decimal, comma-separated)
//              .dw  <w,...>   define 16-bit words (little-endian)
//              .ascii "<str>" embed ASCII string bytes
//
// Opcodes:     NOP            0xEA
//              BRK            0x00
//              LDA #<imm>     0xA9  (load accumulator, immediate)
//              LDA <addr>     0xAD  (load accumulator, absolute)
//              LDA <addr>,X   0xBD  (load accumulator, absolute indexed by X)
//              LDX #<imm>     0xA2  (load X register, immediate)
//              STA <addr>     0x85  (store accumulator, zero-page addr)
//              ADC #<imm>     0x69  (add with carry, immediate)
//              SBC #<imm>     0xE9  (subtract with carry, immediate)
//              AND #<imm>     0x29  (AND accumulator)
//              ORA #<imm>     0x09  (OR accumulator)
//              EOR #<imm>     0x49  (XOR accumulator)
//              CMP #<imm>     0xC9  (compare accumulator)
//              INX            0xE8
//              INY            0xC8
//              DEX            0xCA
//              DEY            0x88
//              TAX            0xAA  (transfer A → X)
//              TAY            0xA8  (transfer A → Y)
//              TXA            0x8A  (transfer X → A)
//              TYA            0x98  (transfer Y → A)
//              JMP <addr>     0x4C  (absolute jump)
//              JSR <addr>     0x20  (jump to subroutine)
//              RTS            0x60  (return from subroutine)
//              BEQ <offset>   0xF0  (branch if equal/zero)
//              BNE <offset>   0xD0  (branch if not equal)
//              BCC <offset>   0x90  (branch if carry clear)
//              BCS <offset>   0xB0  (branch if carry set)
//
// Labels:      name: (colon-terminated)
// Comments:    ; text to end of line

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include "app_state.h"

namespace Assembler {

struct AsmError {
    int    line;
    std::string message;
};

struct AsmResult {
    std::vector<uint8_t> bytes;
    uint32_t             origin;
    std::vector<AsmError> errors;
    bool success() const { return errors.empty(); }
};

// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static bool parse_number(const std::string& tok, uint32_t& out) {
    if (tok.empty()) return false;
    try {
        if (tok[0] == '$')          { out = std::stoul(tok.substr(1), nullptr, 16); return true; }
        if (tok.substr(0,2) == "0x"){ out = std::stoul(tok.substr(2), nullptr, 16); return true; }
        out = std::stoul(tok, nullptr, 10);
        return true;
    } catch (...) { return false; }
}

// Split line into tokens, respecting quoted strings
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quote = false;
    for (char c : line) {
        if (c == '"') { in_quote = !in_quote; cur += c; }
        else if ((c == ' ' || c == '\t' || c == ',') && !in_quote) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------------------
// Two-pass assembler

AsmResult assemble(const std::string& source) {
    AsmResult result;
    result.origin = 0x8000;

    std::vector<std::string> lines;
    {
        std::istringstream ss(source);
        std::string line;
        while (std::getline(ss, line)) lines.push_back(line);
    }

    // Forward declaration for label resolution
    struct PatchSite {
        size_t   byte_offset;   // where in result.bytes to patch
        std::string label;
        bool     is_relative;   // true = branch, false = absolute
        int      line;
        int      pc_after;      // PC after the full instruction (for relative)
    };

    std::unordered_map<std::string, uint32_t> labels; // label → absolute address
    std::vector<PatchSite> patches;
    uint32_t pc = result.origin;

    auto emit = [&](uint8_t b) { result.bytes.push_back(b); pc++; };
    auto emit16 = [&](uint16_t w) { emit(w & 0xFF); emit((w >> 8) & 0xFF); };
    auto err = [&](int ln, const std::string& msg) {
        result.errors.push_back({ln, msg});
    };

    // PASS 1: assemble and collect labels
    for (int ln = 0; ln < (int)lines.size(); ln++) {
        std::string line = lines[ln];

        // Strip comments
        auto cpos = line.find(';');
        if (cpos != std::string::npos) line = line.substr(0, cpos);
        line = trim(line);
        if (line.empty()) continue;

        // Label definition
        if (line.back() == ':') {
            std::string lname = trim(line.substr(0, line.size()-1));
            if (!lname.empty()) labels[lname] = pc;
            continue;
        }

        // Label at start of line followed by instruction
        auto space_pos = line.find(' ');
        if (space_pos != std::string::npos && line.find(':') != std::string::npos &&
            line.find(':') < space_pos) {
            std::string lname = trim(line.substr(0, line.find(':')));
            labels[lname] = pc;
            line = trim(line.substr(line.find(':') + 1));
            if (line.empty()) continue;
        }

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;
        std::string mnem = to_upper(tokens[0]);

        // Directives
        if (mnem == ".ORG") {
            uint32_t addr = 0;
            if (tokens.size() < 2 || !parse_number(tokens[1], addr))
                err(ln+1, ".org requires an address");
            else { result.origin = addr; pc = addr; result.bytes.clear(); }
        }
        else if (mnem == ".DB" || mnem == ".BYTE") {
            for (size_t i = 1; i < tokens.size(); i++) {
                uint32_t v = 0;
                if (!parse_number(tokens[i], v)) err(ln+1, "Invalid byte value: " + tokens[i]);
                else emit(static_cast<uint8_t>(v));
            }
        }
        else if (mnem == ".DW" || mnem == ".WORD") {
            for (size_t i = 1; i < tokens.size(); i++) {
                uint32_t v = 0;
                if (!parse_number(tokens[i], v)) err(ln+1, "Invalid word value: " + tokens[i]);
                else emit16(static_cast<uint16_t>(v));
            }
        }
        else if (mnem == ".ASCII") {
            if (tokens.size() < 2) { err(ln+1, ".ascii needs a quoted string"); continue; }
            std::string s = tokens[1];
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size()-2);
            for (char c : s) emit(static_cast<uint8_t>(c));
        }
        // Implied opcodes
        else if (mnem == "NOP") emit(0xEA);
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
        // Immediate opcodes (#imm)
        else if (mnem == "LDA" || mnem == "ADC" || mnem == "SBC" ||
                 mnem == "AND" || mnem == "ORA" || mnem == "EOR" || mnem == "CMP") {
            if (tokens.size() < 2) { err(ln+1, mnem + " needs operand"); continue; }
            std::string op = tokens[1];
            uint8_t opc = 0;
            if (mnem == "LDA") opc = 0xA9;
            else if (mnem == "ADC") opc = 0x69;
            else if (mnem == "SBC") opc = 0xE9;
            else if (mnem == "AND") opc = 0x29;
            else if (mnem == "ORA") opc = 0x09;
            else if (mnem == "EOR") opc = 0x49;
            else if (mnem == "CMP") opc = 0xC9;
            if (op[0] == '#') {
                uint32_t v = 0;
                if (!parse_number(op.substr(1), v)) err(ln+1, "Invalid immediate: " + op);
                else { emit(opc); emit(static_cast<uint8_t>(v)); }
            } else {
                err(ln+1, mnem + " currently only supports immediate mode (#value)");
            }
        }
        else if (mnem == "LDX") {
            if (tokens.size() < 2) { err(ln+1, "LDX needs operand"); continue; }
            std::string op = tokens[1];
            if (op.empty() || op[0] != '#') {
                err(ln+1, "LDX currently only supports immediate mode (#value)");
                continue;
            }
            uint32_t v = 0;
            if (!parse_number(op.substr(1), v)) err(ln+1, "Invalid immediate: " + op);
            else { emit(0xA2); emit(static_cast<uint8_t>(v)); }
        }
        else if (mnem == "STA") {
            if (tokens.size() < 2) { err(ln+1, "STA needs operand"); continue; }
            uint32_t v = 0;
            if (!parse_number(tokens[1], v)) err(ln+1, "Invalid STA address: " + tokens[1]);
            else { emit(0x85); emit(static_cast<uint8_t>(v)); }
        }
        else if (mnem == "LDY") {
            if (tokens.size() < 2) { err(ln+1, "LDY needs operand"); continue; }
            std::string op = tokens[1];
            if (op[0] == '#') {
                uint32_t v = 0;
                if (!parse_number(op.substr(1), v)) err(ln+1, "Invalid immediate: " + op);
                else { emit(0xA0); emit(static_cast<uint8_t>(v)); }
            } else { err(ln+1, "LDY only supports immediate mode"); }
        }
        else if (mnem == "JMP" || mnem == "JSR") {
            if (tokens.size() < 2) { err(ln+1, mnem + " needs address"); continue; }
            emit(mnem == "JMP" ? 0x4C : 0x20);
            uint32_t v = 0;
            if (parse_number(tokens[1], v)) {
                emit16(static_cast<uint16_t>(v));
            } else {
                // Label reference – emit placeholder, patch later
                patches.push_back({result.bytes.size(), tokens[1], false, ln+1, 0});
                emit16(0);
            }
        }
        else if (mnem == "BEQ" || mnem == "BNE" || mnem == "BCC" || mnem == "BCS") {
            if (tokens.size() < 2) { err(ln+1, mnem + " needs label/offset"); continue; }
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
                int pc_after = pc + 1; // after the offset byte
                patches.push_back({result.bytes.size(), tokens[1], true, ln+1, pc_after});
                emit(0); // placeholder
            }
        }
        else {
            err(ln+1, "Unknown mnemonic: " + mnem);
        }
    }

    // PASS 2: resolve label patches
    for (const auto& p : patches) {
        auto it = labels.find(p.label);
        if (it == labels.end()) {
            result.errors.push_back({p.line, "Undefined label: " + p.label});
            continue;
        }
        uint32_t target = it->second;
        if (p.is_relative) {
            int32_t offset = (int32_t)target - p.pc_after;
            if (offset < -128 || offset > 127) {
                result.errors.push_back({p.line, "Branch out of range to label: " + p.label});
                continue;
            }
            result.bytes[p.byte_offset] = static_cast<uint8_t>(offset & 0xFF);
        } else {
            result.bytes[p.byte_offset]   = target & 0xFF;
            result.bytes[p.byte_offset+1] = (target >> 8) & 0xFF;
        }
    }

    return result;
}

// Convert raw bytes from a bitmap (any format) into palette-indexed ROM data
// pixels: RGBA8888 array, width x height
// Returns bytes equal to width * height (one byte per pixel, palette-mapped)
inline std::vector<uint8_t> bitmap_to_bytes(
    const uint8_t* rgba, int width, int height) {
    std::vector<uint8_t> out;
    out.reserve(width * height);
    for (int i = 0; i < width * height; i++) {
        uint8_t r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2];
        // Simple grayscale mapping to 0-255
        out.push_back((uint8_t)(((int)r + g + b) / 3));
    }
    return out;
}

} // namespace Assembler
