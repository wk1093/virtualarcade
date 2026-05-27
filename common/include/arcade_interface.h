#pragma once
#include <stdint.h>

typedef struct {
    void (*reset)();
    void (*step)(uint8_t* memory);
    uint16_t (*get_pc)();
    const char* (*get_name)();
} CPU_Interface;

typedef CPU_Interface* (*GetCPUInterfaceFunc)();

typedef struct {
    void (*reset)();
    void (*step)(uint8_t* memory);
    void (*render)(uint32_t* framebuffer, int width, int height);
    const char* (*get_name)();
} GPU_Interface;

typedef GPU_Interface* (*GetGPUInterfaceFunc)();