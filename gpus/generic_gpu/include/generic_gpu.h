#pragma once
#include "arcade_interface.h"

// Generic GPU implementation
class GenericGPU {
private:
    uint32_t* framebuffer;
    int width;
    int height;
    uint32_t cycle_count;
    uint16_t scanline;

public:
    GenericGPU();
    ~GenericGPU();
    void reset();
    void step(uint8_t* memory);
    void render(uint32_t* output_framebuffer, int fb_width, int fb_height);
    const char* get_name() const;
};

extern "C" {
    GPU_Interface* get_gpu_interface();
}
