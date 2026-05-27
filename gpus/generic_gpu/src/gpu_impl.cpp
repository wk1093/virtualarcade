#include "generic_gpu.h"
#include <cstring>
#include <iostream>

GenericGPU::GenericGPU() : framebuffer(nullptr), width(256), height(240), cycle_count(0), scanline(0) {
    framebuffer = new uint32_t[width * height];
    std::memset(framebuffer, 0, width * height * sizeof(uint32_t));
}

GenericGPU::~GenericGPU() {
    if (framebuffer) {
        delete[] framebuffer;
        framebuffer = nullptr;
    }
}

void GenericGPU::reset() {
    cycle_count = 0;
    scanline = 0;
    std::memset(framebuffer, 0, width * height * sizeof(uint32_t));
}

void GenericGPU::step(uint8_t* memory) {
    if (!memory) return;

    // Simulate a simple scanline rasterizer
    cycle_count++;

    // Very basic pattern: fill with gradient color based on scanline
    uint32_t color = (scanline << 16) | (cycle_count & 0xFF);

    // Draw one pixel per cycle
    if (cycle_count < width) {
        int pixel_index = (scanline * width) + (cycle_count - 1);
        if (pixel_index < (width * height)) {
            framebuffer[pixel_index] = color;
        }
    } else {
        // Advance to next scanline
        cycle_count = 0;
        scanline++;
        if (scanline >= height) {
            scanline = 0;
        }
    }
}

void GenericGPU::render(uint32_t* output_framebuffer, int fb_width, int fb_height) {
    if (!output_framebuffer) return;

    // Copy internal framebuffer to output, scaling if necessary
    int copy_width = (fb_width < width) ? fb_width : width;
    int copy_height = (fb_height < height) ? fb_height : height;

    for (int y = 0; y < copy_height; y++) {
        std::memcpy(&output_framebuffer[y * fb_width], &framebuffer[y * width], 
                    copy_width * sizeof(uint32_t));
    }
}

const char* GenericGPU::get_name() const {
    return "Generic GPU";
}

// Static instance
static GenericGPU* g_gpu = nullptr;

// C Interface Functions
static void gpu_reset() {
    if (g_gpu) g_gpu->reset();
}

static void gpu_step(uint8_t* memory) {
    if (g_gpu) g_gpu->step(memory);
}

static void gpu_render(uint32_t* framebuffer, int width, int height) {
    if (g_gpu) g_gpu->render(framebuffer, width, height);
}

static const char* gpu_get_name() {
    return g_gpu ? g_gpu->get_name() : "Unknown";
}

extern "C" {
    GPU_Interface* get_gpu_interface() {
        if (!g_gpu) {
            g_gpu = new GenericGPU();
        }

        static GPU_Interface interface = {
            gpu_reset,
            gpu_step,
            gpu_render,
            gpu_get_name
        };

        return &interface;
    }
}
