#pragma once
#include "arcade_interface.h"

// Generic CPU implementation
class GenericCPU {
private:
    uint16_t pc;           // Program Counter
    uint8_t a;             // Accumulator
    uint8_t x, y;          // Index registers
    uint8_t sp;            // Stack Pointer
    uint8_t status;        // Status Flags
    uint32_t cycle_count;

public:
    GenericCPU();
    void reset();
    void step(uint8_t* memory);
    uint16_t get_pc() const;
    const char* get_name() const;
};

extern "C" {
    CPU_Interface* get_cpu_interface();
}
