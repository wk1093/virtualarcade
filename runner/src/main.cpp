#include <iostream>
#include <vector>
#include <dlfcn.h>
#include <cstring>
#include "arcade_interface.h"
#include "config.h"

struct LoadedComponent {
    std::string name;
    std::string type;
    void* handle;
    union {
        CPU_Interface* cpu_interface;
        GPU_Interface* gpu_interface;
    } interface;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: runner <config.json>" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];

    try {
        // Load configuration
        std::cout << "Loading motherboard configuration from: " << config_path << std::endl;
        MotherboardConfig config = MotherboardConfig::from_json_file(config_path);

        std::cout << "Motherboard: " << config.name << " (v" << config.version << ")" << std::endl;
        std::cout << "Master Clock: " << config.master_clock << " Hz" << std::endl;

        // Initialize RAM (64KB default)
        std::vector<uint8_t> ram(65536, 0);
        std::cout << "Initialized RAM: " << ram.size() << " bytes" << std::endl;

        // Load components
        std::vector<LoadedComponent> components;
        CPU_Interface* cpu_iface = nullptr;
        GPU_Interface* gpu_iface = nullptr;

        for (const auto& comp_config : config.components) {
            std::cout << "Loading component: " << comp_config.name << " (" << comp_config.type << ")" << std::endl;

            // Open dynamic library
            void* handle = dlopen(comp_config.library_path.c_str(), RTLD_LAZY);
            if (!handle) {
                std::cerr << "Error loading library " << comp_config.library_path << ": " << dlerror() << std::endl;
                continue;
            }

            LoadedComponent comp;
            comp.name = comp_config.name;
            comp.type = comp_config.type;
            comp.handle = handle;

            if (comp_config.type == "cpu") {
                // Load CPU interface
                GetCPUInterfaceFunc get_cpu = (GetCPUInterfaceFunc)dlsym(handle, "get_cpu_interface");
                if (!get_cpu) {
                    std::cerr << "Error: Could not find 'get_cpu_interface' in library" << std::endl;
                    dlclose(handle);
                    continue;
                }

                comp.interface.cpu_interface = get_cpu();
                if (comp.interface.cpu_interface) {
                    cpu_iface = comp.interface.cpu_interface;
                    std::cout << "  CPU: " << cpu_iface->get_name() << std::endl;
                    cpu_iface->reset();
                }
            } else if (comp_config.type == "gpu") {
                // Load GPU interface
                GetGPUInterfaceFunc get_gpu = (GetGPUInterfaceFunc)dlsym(handle, "get_gpu_interface");
                if (!get_gpu) {
                    std::cerr << "Error: Could not find 'get_gpu_interface' in library" << std::endl;
                    dlclose(handle);
                    continue;
                }

                comp.interface.gpu_interface = get_gpu();
                if (comp.interface.gpu_interface) {
                    gpu_iface = comp.interface.gpu_interface;
                    std::cout << "  GPU: " << gpu_iface->get_name() << std::endl;
                    gpu_iface->reset();
                }
            }

            components.push_back(comp);
        }

        if (!cpu_iface) {
            std::cerr << "Error: No CPU loaded!" << std::endl;
            return 1;
        }

        if (!gpu_iface) {
            std::cerr << "Error: No GPU loaded!" << std::endl;
            return 1;
        }

        // Main emulation loop
        std::cout << "\n=== Starting Emulation ===" << std::endl;
        uint32_t framebuffer[256 * 240];
        std::memset(framebuffer, 0, sizeof(framebuffer));

        // Run for a limited number of cycles for testing
        for (int cycle = 0; cycle < 1000; cycle++) {
            // Execute CPU step
            cpu_iface->step(ram.data());

            // Execute GPU step
            gpu_iface->step(ram.data());

            // Render GPU
            gpu_iface->render(framebuffer, 256, 240);

            if (cycle % 100 == 0) {
                std::cout << "Cycle " << cycle << " - PC: 0x" << std::hex << cpu_iface->get_pc() << std::dec << std::endl;
            }
        }

        std::cout << "=== Emulation Complete ===" << std::endl;
        std::cout << "Final PC: 0x" << std::hex << cpu_iface->get_pc() << std::dec << std::endl;

        // Cleanup
        for (auto& comp : components) {
            dlclose(comp.handle);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
