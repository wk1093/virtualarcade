#pragma once
#include <stdint.h>

typedef enum {
    CPU_BUILD_LOG_INFO = 0,
    CPU_BUILD_LOG_WARNING = 1,
    CPU_BUILD_LOG_ERROR = 2,
} CPU_BuildLogLevel;

typedef void (*CPU_BuildLogFn)(int level, const char* message, void* user_data);

typedef struct {
    const char* id;               // stable id, e.g. "asm.generic"
    const char* display_name;     // shown in UI, e.g. "Generic ASM"
    const char* file_extensions;  // optional, e.g. ".asm;.s"
    const char* syntax_keywords;  // optional, space-separated keyword hint list
} CPU_LanguageDescriptor;

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t origin;
} CPU_BinaryOutput;

typedef struct {
    void (*reset)();
    void (*step)(uint8_t* memory);
    uint16_t (*get_pc)();
    const char* (*get_name)();

    // Optional toolchain APIs. If null, editor should treat as unsupported.
    uint32_t (*get_language_count)();
    const CPU_LanguageDescriptor* (*get_language_descriptor)(uint32_t index);
    int (*build_source)(
        const char* language_id,
        const char* source,
        CPU_BinaryOutput* output,
        CPU_BuildLogFn log_fn,
        void* user_data
    );
    void (*free_binary_output)(CPU_BinaryOutput* output);
} CPU_Interface;

typedef CPU_Interface* (*GetCPUInterfaceFunc)();

typedef struct {
    void (*reset)();
    void (*step)(uint8_t* memory);
    void (*render)(uint32_t* framebuffer, int width, int height);
    const char* (*get_name)();
} GPU_Interface;

typedef GPU_Interface* (*GetGPUInterfaceFunc)();