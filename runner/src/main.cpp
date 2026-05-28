#include <iostream>
#include <vector>
#include <dlfcn.h>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits.h>
#include <unistd.h>
#include "arcade_interface.h"
#include "arcade_spec.h"
#include "config.h"
#include "image_format.h"

namespace fs = std::filesystem;

// Get the directory containing the executable
std::string get_executable_dir(const char* argv0) {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        result[count] = '\0';
        std::string exePath(result);
        return exePath.substr(0, exePath.find_last_of("/\\"));
    }
    // Fallback to argv0 if /proc/self/exe doesn't work
    std::string argvPath(argv0);
    return argvPath.substr(0, argvPath.find_last_of("/\\"));
}

struct LoadedComponent {
    std::string name;
    std::string type;
    void* handle;
    json spec;
    json effective_config;  // Spec + overrides merged
    
    union {
        CPU_Interface* cpu_interface;
        GPU_Interface* gpu_interface;
    } interface;
    union {
        CPU_Specification* cpu_spec;
        GPU_Specification* gpu_spec;
    } spec_data;
};

json load_spec_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open spec file: " + filepath);
    }
    json j;
    file >> j;
    return j;
}

std::string find_library_for_component(const std::string& type, const std::string& name, 
                                        const std::string& data_dir) {
    // Look for libname.so in data/type/name/
    std::string libdir = data_dir + "/" + type + "/" + name + "/";
    std::string libname = libdir + std::string("lib") + name + ".so";
    
    if (fs::exists(libname)) {
        return libname;
    }
    
    // Alternative: just name.so
    libname = libdir + name + ".so";
    if (fs::exists(libname)) {
        return libname;
    }
    
    throw std::runtime_error("Could not find library for component: " + type + "/" + name);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: runner <motherboard.json> [rom.img]" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];
    std::string rom_path = (argc >= 3) ? argv[2] : "";
    
    // Get the executable directory and construct the data directory path
    std::string exe_dir = get_executable_dir(argv[0]);
    std::string data_dir = exe_dir + "/data";

    try {
        // Load configuration
        std::cerr << "Loading motherboard configuration from: " << config_path << std::endl;
        MotherboardConfig config = MotherboardConfig::from_json_file(config_path);

        std::cerr << "Motherboard: " << config.name << " (v" << config.version << ")" << std::endl;
        std::cerr << "Master Clock: " << config.master_clock << " Hz" << std::endl;

        // Load and merge component specs
        std::vector<LoadedComponent> components;
        uint32_t total_memory = 0;
        std::vector<json> memory_specs;

        std::cerr << "\n=== Loading Components ===" << std::endl;
        
        for (const auto& comp_ref : config.components) {
            std::string spec_path = data_dir + "/" + comp_ref.type + "/" + 
                                   comp_ref.name + "/" + comp_ref.type + ".json";
            
            std::cerr << "Loading " << comp_ref.type << "/" << comp_ref.name << "..." << std::endl;
            
            // Load spec file
            json spec = load_spec_file(spec_path);
            
            // Merge overrides
            json effective_config = spec;
            for (auto& [key, value] : comp_ref.overrides.items()) {
                effective_config[key] = value;
            }
            
            LoadedComponent comp;
            comp.name = comp_ref.name;
            comp.type = comp_ref.type;
            comp.spec = spec;
            comp.effective_config = effective_config;
            
            // Track memory components for later
            if (comp_ref.type == "ram" || comp_ref.type == "rom") {
                json mem_info = effective_config;
                
                // Check if we need to extract from defaults
                uint32_t start = mem_info.value("start_address", 0);
                uint32_t size = mem_info.value("size", 0);
                
                if (start == 0 && size == 0 && mem_info.contains("defaults")) {
                    start = mem_info["defaults"].value("start_address", 0);
                    size = mem_info["defaults"].value("size", 0);
                }
                
                // Flatten the config for easier access later
                json flattened;
                flattened["type"] = comp_ref.type;
                flattened["name"] = comp_ref.name;
                flattened["start_address"] = start;
                flattened["size"] = size;
                memory_specs.push_back(flattened);
                
                if (start + size > total_memory) {
                    total_memory = start + size;
                }
                
                std::cerr << "  " << effective_config.value("name", "unknown") << ": " 
                          << size << " bytes @ 0x" << std::hex << start << std::dec << std::endl;
            }
            
            components.push_back(comp);
        }

        // Create unified memory space
        std::cerr << "\nTotal memory space: " << total_memory << " bytes" << std::endl;
        std::vector<uint8_t> ram(total_memory, 0);

        // Try to load ROM images from .img file if it exists
        // Use provided ROM path if given, otherwise fall back to data/rom.img
        std::string img_file = rom_path.empty() ? (data_dir + "/rom.img") : rom_path;
        if (fs::exists(img_file)) {
            try {
                std::cerr << "\n=== Loading ROM Images ===" << std::endl;
                VarcadeImage::ImageFile imgf = VarcadeImage::ImageFile::load(img_file);
                
                for (size_t i = 0; i < imgf.headers.size(); i++) {
                    const auto& header = imgf.headers[i];
                    const auto& image_data = imgf.images[i];
                    
                    // Find the corresponding ROM memory spec
                    for (const auto& mem_spec : memory_specs) {
                        if (mem_spec.value("type", "") == "rom") {
                            uint32_t rom_start = mem_spec.value("start_address", 0);
                            uint32_t rom_size = mem_spec.value("size", 0);
                            uint32_t copy_size = std::min(static_cast<uint32_t>(image_data.size()), rom_size);
                            
                            if (rom_start + copy_size <= total_memory) {
                                std::cerr << "Loading ROM image: " << header.name 
                                          << " (" << copy_size << " bytes @ 0x" 
                                          << std::hex << rom_start << std::dec << ")" << std::endl;
                                std::memcpy(ram.data() + rom_start, image_data.data(), copy_size);
                                break;
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not load ROM images: " << e.what() << std::endl;
            }
        }

        // Load dynamic libraries
        std::cerr << "\n=== Loading Dynamic Libraries ===" << std::endl;
        CPU_Interface* cpu_iface = nullptr;
        GPU_Interface* gpu_iface = nullptr;

        for (auto& comp : components) {
            if (comp.type != "ram" && comp.type != "rom") {
                std::string libpath = find_library_for_component(comp.type, comp.name, data_dir);
                std::cerr << "Loading library: " << libpath << std::endl;
                
                void* handle = dlopen(libpath.c_str(), RTLD_LAZY);
                if (!handle) {
                    std::cerr << "Error loading library: " << dlerror() << std::endl;
                    continue;
                }
                
                comp.handle = handle;
                
                if (comp.type == "cpu") {
                    GetCPUInterfaceFunc get_cpu = (GetCPUInterfaceFunc)dlsym(handle, "get_cpu_interface");
                    if (!get_cpu) {
                        std::cerr << "Error: Could not find 'get_cpu_interface'" << std::endl;
                        dlclose(handle);
                        continue;
                    }
                    
                    comp.interface.cpu_interface = get_cpu();
                    cpu_iface = comp.interface.cpu_interface;
                    
                    // Try to load spec function
                    GetCPUSpecFunc get_spec = (GetCPUSpecFunc)dlsym(handle, "get_cpu_spec");
                    if (get_spec) {
                        comp.spec_data.cpu_spec = get_spec();
                    }
                    
                    std::cerr << "  CPU: " << cpu_iface->get_name() << std::endl;
                    cpu_iface->reset();
                    
                } else if (comp.type == "gpu") {
                    GetGPUInterfaceFunc get_gpu = (GetGPUInterfaceFunc)dlsym(handle, "get_gpu_interface");
                    if (!get_gpu) {
                        std::cerr << "Error: Could not find 'get_gpu_interface'" << std::endl;
                        dlclose(handle);
                        continue;
                    }
                    
                    comp.interface.gpu_interface = get_gpu();
                    gpu_iface = comp.interface.gpu_interface;
                    
                    // Try to load spec function
                    GetGPUSpecFunc get_spec = (GetGPUSpecFunc)dlsym(handle, "get_gpu_spec");
                    if (get_spec) {
                        comp.spec_data.gpu_spec = get_spec();
                    }
                    
                    std::cerr << "  GPU: " << gpu_iface->get_name() << std::endl;
                    gpu_iface->reset();
                }
            }
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
        std::cerr << "\n=== Starting Emulation ===" << std::endl;
        uint32_t framebuffer[256 * 240];
        std::memset(framebuffer, 0, sizeof(framebuffer));
        constexpr uint16_t MMIO_STDOUT_ADDR = 0x00F0;

        // Run for a limited number of cycles for testing
        for (int cycle = 0; cycle < 1000; cycle++) {
            uint16_t pc_before = cpu_iface->get_pc();
            uint8_t opcode_before = ram[pc_before];

            if (cycle % 100 == 0) {
                std::cerr << "Cycle " << cycle << " - PC: 0x" << std::hex << pc_before << std::dec << std::endl;
            }

            // Stop once BRK is reached so we don't march through zero-filled memory.
            if (opcode_before == 0x00) {
                std::cerr << "BRK encountered at PC: 0x" << std::hex << pc_before << std::dec << std::endl;
                break;
            }

            // Execute CPU step
            cpu_iface->step(ram.data());

            // MMIO terminal output: write the byte as ASCII to stdout.
            if (MMIO_STDOUT_ADDR < ram.size() && ram[MMIO_STDOUT_ADDR] != 0) {
                uint8_t out = ram[MMIO_STDOUT_ADDR];
                std::cout.put(static_cast<char>(out));
                std::cout.flush();
                ram[MMIO_STDOUT_ADDR] = 0;
            }

            // Execute GPU step
            gpu_iface->step(ram.data());

            // Render GPU
            gpu_iface->render(framebuffer, 256, 240);

        }

        std::cerr << "=== Emulation Complete ===" << std::endl;
        std::cerr << "Final PC: 0x" << std::hex << cpu_iface->get_pc() << std::dec << std::endl;

        // Cleanup
        for (auto& comp : components) {
            if (comp.handle) {
                dlclose(comp.handle);
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
