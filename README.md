# Virtual Arcade Engine (VAE)

**VAE** is a modular, high-performance platform for creating, simulating, and "modding" virtual computer hardware. It is an "Engineer’s Sandbox" that allows users to design custom motherboard architectures, select processors, and build cycle-accurate retro-style arcade games.

---

## Architecture Overview

VAE is built as a **plugin-based system**. The platform separates the **Tooling (Editor)** from the **Execution (Runner)** and delegates hardware emulation to **Dynamic Libraries**.

### Key Components

* **The Runner (Dispatcher):** A thin, high-performance C engine that acts as the "Hardware Bus Controller." It manages memory mapping, enforces clock synchronization via a "Clock Governor," and coordinates communication between hardware plugins.
* **The Editor:** A separate executable that acts as the IDE. It provides a visual interface for motherboard design, sprite editing, and code management.
* **The Plugin Ecosystem:** CPUs, GPUs, and peripherals are built as independent dynamic libraries (`.so` or `.dll`). This allows the system to remain extensible—new hardware architectures can be added without modifying the engine core.
* **The Common SDK:** A shared header-only interface (`common/`) that defines the "Contract" between the Runner and the hardware plugins, ensuring type safety and modularity.

---

## The Motherboard Design

Instead of hardcoding hardware, VAE uses **JSON-based Schematics**. Users define their system configuration, including memory maps and clock signal routing.

### Clock Distribution Network

VAE uses a **Clock Governor** system. Users define a Master Crystal Oscillator and set ratios for system components.

* **Example:** Connect a `6502 CPU` at `1/12` speed and a `2C02 PPU` at `1/4` speed relative to the master clock.
* The Runner handles the phase-locking and synchronization automatically, allowing for cycle-accurate emulation without the overhead of gate-level circuit simulation.

---

## Project Structure

VAE is organized as a modular Monorepo to facilitate independent development and testing of hardware modules.

```text
root/
├── CMakeLists.txt
├── common/               # Shared interface SDK
├── runner/               # Core dispatcher and VM runtime
├── editor/               # IDE, UI, and design tools
├── cpus/                 # Plugin directory: z80/, 6502/, etc.
└── gpus/                 # Plugin directory: tile_gpu/, etc.

```

---

## Key Features

* **1:1 Accuracy Potential:** By utilizing a cycle-synchronous dispatcher, VAE can support 1:1 hardware recreation (e.g., NES/2C02) when paired with accurate CPU/PPU plugins.
* **Dynamic Plugin System:** Add new hardware support by dropping a library into the `plugins/` folder. The platform auto-discovers and integrates the hardware into the motherboard UI.
* **Steam-Ready Modding:** Because configurations are stored as simple JSON/TOML, users can easily share their "Motherboard Carts" and hardware mods via Steam Workshop.
* **Developer Friendly:** Includes an internal "Logic Analyzer" and "Memory Viewer" to help engineers debug their virtual machines in real-time.
* **Fully Dynamic:** Even basic memory chips are implemented as data, all chips are defined by json files (some like the cpu/gpu also have a dynamic library), and the runner is a thin dispatcher that can run any configuration of hardware as long as it follows the common sdk contract.

---

## Getting Started

1. **Clone the Repository:** Ensure you have CMake 3.20+ and a C++20 compatible compiler.
2. **Build:** 
```bash
mkdir build && cd build
cmake ..
make
```



3. **Create:** Open the `editor` executable to design your first motherboard, define your memory map, and load your binary assets.
4. **Run:** Execute the `runner` with your project directory as an argument to launch your virtual arcade cabinet.
