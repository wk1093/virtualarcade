#include <iostream>
#include <vector>
#include "arcade_interface.h"

int main(int argc, char* argv[]) {
    // 1. Initialize RAM (64KB)
    std::vector<uint8_t> ram(65536, 0);

    std::cout << "Runner starting... Awaiting binary load." << std::endl;

    // 2. Here you would use dlopen() or LoadLibrary() 
    // to load a CPU plugin from a path provided in argv[1]
    
    return 0;
}