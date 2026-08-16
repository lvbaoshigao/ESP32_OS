# ESP32-C3 Minimal Embedded OS Kernel — Feature Extension and Structural Optimization Task Specification

## 1. Project Background
- **Hardware Platform**: ESP32-C3 (RISC-V core, 400KB SRAM, SPI Flash, no PSRAM).
- **Existing Achievements**: First-stage bootloader, bare-metal environment (no ESP-IDF), basic kernel framework (startup, interrupts, task scheduler, memory management, IPC), UART debug output.
- **Current Goal**: While maintaining the stability of the existing kernel, **add a series of core subsystems** and **refactor the code structure** (introduce C++ HAL layer, clear layering). All new features must be independently compilable and testable, and ultimately run on real hardware.

## 2. Overall Design Principles
- **Modularity**: Each function exists as an independent library/directory, providing a unified header file (`*.hpp`), with internal implementations invisible to the outside.
- **Hardware Abstraction Layer (HAL)**: All register operations use C++ class encapsulation (e.g., `Uart`, `Gpio`, `Spi`, `Timer`) to facilitate unit testing and driver replacement.
- **Observability**: The kernel startup process outputs detailed logs (`KLOG_INFO`, `KLOG_DEBUG`), which can later be silenced via `klog_set_level()`.
- **Memory Safety**: All dynamic memory allocations must go through the kernel memory manager (`mm_alloc`/`mm_free`), with consideration for fragmentation issues.
- **Test-First**: Each module must complete functional verification in a QEMU (`qemu-system-riscv32`) simulation environment before being flashed to a real ESP32-C3 for final acceptance.

## 3. List of New Functional Modules (Sorted by Priority)

### 🔴 P0 – Must Complete (Core Foundation)
| Module | Functional Description | Acceptance Criteria |
|--------|------------------------|---------------------|
| **Preemptive Scheduler** | Based on TIMG timer interrupt, implements time-slice round-robin + priority preemption. Must save/restore context (integer + floating point, if enabled). | Create two tasks with different priorities; the high-priority task can preempt the low-priority one, and the log shows the number of context switches. |
| **Memory Management Extension** | Implement `mm_swap_init()` and `mm_zram_init()` framework, reserving the LZ4 compression interface (compression not implemented for now). Provide stub functions for page swap-out/swap-in. | Calling initialization does not crash; `mm_swap_info()` returns correct status. |
| **VFS & FatFS Porting** | Port FatFS (R0.15) and implement a VFS layer (`open`, `read`, `write`, `close`, `lseek`). Support a file system on SPI Flash (requires implementing the diskio bottom-level driver). | Create a file on Flash, read/write, and data is retained after reboot. |
| **Network Stack (LwIP)** | Bare-metal port of LwIP 2.1.3, implement Wi-Fi driver (calling Espressif ROM binary rf functions). Requirement: initialize in AP mode, SSID: `ESP32`, password: `12345678`. | A mobile phone can discover and connect to the hotspot; kernel log shows successful connection. |

### 🟡 P1 – Important Enhancements
| Module | Functional Description | Acceptance Criteria |
|--------|------------------------|---------------------|
| **User and Permission Management** | Implement user creation/deletion/password modification, support `root` and ordinary user permissions. System calls must check permissions. | Create an ordinary user; after switching users, access to root-exclusive files is denied (if FS permission bits are implemented). |
| **Security and Exception Handling** | Implement "Safe Mode" (disable interrupts), "Emergency Mode" (minimal system recovery). Configure PMP (Physical Memory Protection) to isolate kernel and user mode (if user mode is enabled). | Trigger an illegal memory access; PMP captures it and enters emergency mode, with log output. |
| **Partition Tool** | Provide a command-line tool (or initialization at startup) to parse the Flash partition table, supporting simple MBR/GPT-style partition reading. | Able to list partition names, start addresses, and sizes. |

### 🟢 P2 – Optional (But Highly Recommended)
- **HTTP Boot Interface**: After Wi-Fi connection, access a web page via DNS or IP (e.g., `192.168.4.1`), displaying kernel boot logs in white text on a black background (real-time streaming).
- **Shell Interaction**: Provide a simple command line via UART for easy debugging and manual testing of each module.

## 4. Planned Code Structure
```
src/
├── core/                 # Kernel core (existing)
│   ├── task.c
│   ├── mm.c
│   ├── ipc.c
│   └── ...
├── hal/                  # Hardware Abstraction Layer (C++)
│   ├── uart.hpp
│   ├── gpio.hpp
│   ├── spi.hpp
│   ├── timer.hpp
│   └── pmu.hpp
├── drivers/              # Device drivers (based on HAL)
│   ├── wifi/
│   │   ├── esp32c3_wifi.cpp
│   │   └── rf_rom_api.h   # Declare ROM function pointers
│   ├── flash/
│   │   └── spi_flash.cpp
│   └── uart/
├── fs/                   # File system
│   ├── vfs.hpp
│   ├── fatfs/
│   └── diskio/
├── net/                  # Network protocol stack
│   ├── lwip/
│   └── netif/
├── syscall/              # System call interface
│   ├── syscall_table.c
│   └── user_api.h
├── security/             # Security & PMP
│   ├── pmp.c
│   └── safe_mode.c
└── init/                 # Initialization flow
    ├── kernel_init.c
    └── module_init.c
```

Each subdirectory should contain a `CMakeLists.txt` or `Makefile` for independent compilation, with a top-level Makefile for unified linking.

## 5. Development and Testing Requirements
- **Development Process**:
  1. First implement hardware-independent logic in QEMU (e.g., VFS layer, scheduler simulation).
  2. Write HAL drivers specific to ESP32-C3, using `esptool.py` to flash to `0x0`.
  3. Before each commit, execute static checks provided in `./AI/help` (e.g., `clang-format`, `cppcheck`).
- **Logging Conventions**:
  - `[INIT]` – Initialization phase
  - `[SCHED]` – Scheduling related
  - `[NET]` – Network events
  - `[FS]` – File operations
  - `[ERR]` – Errors and exceptions
- **Test Cases**:
  - Each module provides a `test_<module>.c` or standalone test program, outputting test results via UART.
  - Example: `test_fs` will execute `open("/test.txt", O_CREAT)` and verify by writing and reading.

## 6. Deliverables Checklist
- [ ] Complete source code (with comments), conforming to the above directory structure.
- [ ] One-click build script (`build.sh`) and flash script (`flash.sh`).
- [ ] Boot log sample (`log_boot.txt`) demonstrating the initialization process of all modules.
- [ ] Independent test reports for each module (QEMU and real hardware screenshots/logs).
- [ ] Brief design document (`docs/design.md`) describing module interfaces and dependencies.

## 7. Technical Detail Requirements (For Reference)
- **Wi-Fi RF**: Espressif provides `liblibrf.a` and header files, calling `rf_phy_init`, `rf_phy_start` etc. via function pointers. Ensure correct memory allocation.
- **FatFS Configuration**: `FF_USE_LFN` enabled for long file names, `FF_FS_EXFAT` disabled to save memory.
- **LwIP Configuration**: Enable only TCP/IP, disable UDP checksum, adapt memory pool size for 400KB.
- **PMP**: ESP32-C3 supports 8 PMP regions, used to isolate kernel code and data, and protect critical registers.

## 8. Risk Warnings
- **Stack Overflow**: Interrupt stack and task stacks need to be sized appropriately (recommended 1KB~2KB).
- **Timer Accuracy**: TIMG interrupt priority should be set to the highest (for preemptive scheduling).
- **ROM Function Compatibility**: Ensure parameters and memory alignment meet requirements when calling Espressif functions.

---

**Please carry out development according to this specification. If any ambiguity arises, refer to the documentation in `./AI/help` first, and output clear design ideas and code. All functionalities must run stably on a real ESP32-C3 eventually.**