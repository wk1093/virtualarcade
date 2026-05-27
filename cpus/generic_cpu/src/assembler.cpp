#include "assembler.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

std::string GenericAssembler::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string GenericAssembler::to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool GenericAssembler::parse_number(const std::string& tok, uint32_t& out) {
    if (tok.empty()) return false;
    try {
        if (tok[0] == '$') {
            out = std::stoul(tok.substr(1), nullptr, 16);
            return true;
        }
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

std::vector<std::string> GenericAssembler::tokenize(const std::string& line) {
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

AsmResult GenericAssembler::assemble(const std::string& source) {
    AsmResult result;

    std::vector<std::string> lines;
    {
        std::istringstream ss(source);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
    }

    std::unordered_map<std::string, uint32_t> labels;
    std::vector<PatchSite> patches;
    uint32_t pc = result.origin;

    // Lambda functions for code emission
    auto emit = [&](uint8_t b) { result.bytes.push_back(b); pc++; };
    auto emit16 = [&](uint16_t w) {
        emit(static_cast<uint8_t>(w & 0xFF));
        emit(static_cast<uint8_t>((w >> 8) & 0xFF));
    };
    auto err = [&](int ln, const std::string& msg) {
        result.errors.push_back({ln, msg});
    };

    // First pass: parse labels and instructions
    for (int ln = 0; ln < static_cast<int>(lines.size()); ln++) {
        std::string line = lines[ln];

        // Remove comments
        auto cpos = line.find(';');
        if (cpos != std::string::npos) {
            line = line.substr(0, cpos);
        }
        
        line = trim(line);
        if (line.empty()) continue;

        // Handle label-only lines
        if (line.back() == ':') {
            std::string lname = trim(line.substr(0, line.size() - 1));
            if (!lname.empty()) {
                labels[lname] = pc;
            }
            continue;
        }

        // Handle inline labels
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

        // Process directives and instructions
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
                if (!parse_number(tokens[i], v)) {
                    err(ln + 1, "Invalid byte value: " + tokens[i]);
                } else {
                    emit(static_cast<uint8_t>(v));
                }
            }
        } else if (mnem == ".DW" || mnem == ".WORD") {
            for (size_t i = 1; i < tokens.size(); i++) {
                uint32_t v = 0;
                if (!parse_number(tokens[i], v)) {
                    err(ln + 1, "Invalid word value: " + tokens[i]);
                } else {
                    emit16(static_cast<uint16_t>(v));
                }
            }
        } else if (mnem == ".ASCII") {
            if (tokens.size() < 2) {
                err(ln + 1, ".ascii needs a quoted string");
                continue;
            }
            std::string s = tokens[1];
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                s = s.substr(1, s.size() - 2);
            }
            for (char c : s) {
                emit(static_cast<uint8_t>(c));
            }
        } else if (mnem == "NOP") {
            emit(0xEA);
        } else if (mnem == "BRK") {
            emit(0x00);
        } else if (mnem == "INX") {
            emit(0xE8);
        } else if (mnem == "INY") {
            emit(0xC8);
        } else if (mnem == "DEX") {
            emit(0xCA);
        } else if (mnem == "DEY") {
            emit(0x88);
        } else if (mnem == "TAX") {
            emit(0xAA);
        } else if (mnem == "TAY") {
            emit(0xA8);
        } else if (mnem == "TXA") {
            emit(0x8A);
        } else if (mnem == "TYA") {
            emit(0x98);
        } else if (mnem == "RTS") {
            emit(0x60);
        } else if (mnem == "LDA" || mnem == "ADC" || mnem == "SBC" ||
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
            if (!parse_number(op.substr(1), v)) {
                err(ln + 1, "Invalid immediate: " + op);
            } else {
                emit(opc);
                emit(static_cast<uint8_t>(v));
            }
        } else if (mnem == "STA") {
            if (tokens.size() < 2) {
                err(ln + 1, "STA needs operand");
                continue;
            }
            uint32_t v = 0;
            if (!parse_number(tokens[1], v)) {
                err(ln + 1, "Invalid STA address: " + tokens[1]);
            } else {
                emit(0x85);
                emit(static_cast<uint8_t>(v));
            }
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

    // Second pass: resolve labels
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
