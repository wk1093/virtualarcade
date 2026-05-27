#pragma once
#include <string>
#include <vector>
#include <cstdint>

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

class GenericAssembler {
public:
    /**
     * Assemble generic CPU assembly code into machine code
     * 
     * Supports:
    * - Mnemonics: NOP, BRK, LDA, LDX, STA, ADC, SBC, AND, ORA, EOR, CMP,
    *              INX, INY, DEX, DEY, TAX, TAY, TXA, TYA, JMP, JSR, RTS,
     *              BEQ, BNE, BCC, BCS
     * - Directives: .ORG, .DB, .DW, .ASCII
     * - Labels with absolute and relative addressing
     * 
     * @param source Assembly source code
     * @return Assembly result with bytecode and error information
     */
    static AsmResult assemble(const std::string& source);

private:
    struct PatchSite {
        size_t byte_offset;
        std::string label;
        bool is_relative;
        int line;
        int pc_after;
    };

    // Helper functions
    static std::string trim(const std::string& s);
    static std::string to_upper(std::string s);
    static bool parse_number(const std::string& tok, uint32_t& out);
    static std::vector<std::string> tokenize(const std::string& line);
};
