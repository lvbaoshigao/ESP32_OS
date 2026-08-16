### Core Positioning
**Focus on Boot Development**: Handle only the boot process from hardware reset to kernel loading, without involving later stages such as the kernel, drivers, or GUI. For all operations, the English documents in the `./prompt/boot/` directory shall serve as the primary reference (Chinese documents may be consulted only for auxiliary understanding when English content is difficult to comprehend).

---

### Project Overview
- **Objective**: Develop a bootable operating system for the ESP32-C6
- **Current Phase**: Boot development (Phase 1)
- **Hardware Platform**: ESP32-C6 (QFN40) rev v0.1
- **Development Languages**: Assembly language (core logic, highest priority), C++ (auxiliary logic), Python (debugging tools and test scripts only)

---

### Hardware Specifications
| Component | Specifications |
|-----------|----------------|
| CPU | ESP32-C6 single-core + LP core |
| Frequency | 160MHz |
| Instruction Set | RISC-V 32-bit (RV32IMAC) |
| Crystal Oscillator | 40MHz |
| SRAM | 512KB + 16KB LP SRAM |
| ROM | 32KB |
| Wireless | Wi-Fi 6, BT 5 (LE), IEEE 802.15.4 (not initialized during boot phase) |

---

### Boot Development Requirements

#### Core Functions
1. **Hardware Initialization**: Minimal initialization (UART0, SPI Flash, basic clock system configuration)
2. **Execution Environment Setup**: Set up stack pointer (SP), interrupt vector table base address (MTVT), and global pointer (GP)
3. **Serial Debug Output**: Output debug logs with timestamps and stage identifiers via UART0
4. **Kernel Probing and Loading**: Read the kernel image from Flash address `0x1000` and verify integrity
5. **Control Transfer**: Jump to the kernel entry point, passing boot parameters (boot mode, hardware information, etc.)
6. **API Interface**: Provide a bidirectional communication mechanism between the bootloader and the kernel

#### Communication Mechanism
- The kernel may send feedback to the bootloader after starting (status codes, mode switch requests, etc.)
- The bootloader dynamically selects boot modes based on feedback (normal, safe, recovery, factory, etc.)
- Support for boot source selection (main Flash, backup partitions, external storage)
- Built-in emergency mode (automatically enters recovery flow upon boot failure detection)

---

### Development Constraints

#### Permitted
- Python, C++, Assembly language (RISC-V 32-bit)
- All development tools and IDE plugins (unrestricted)
- Linux kernel source code (`/media/tip/SSD/linux-7.0.1`) – for design concept reference only, copying code is strictly prohibited
- Local help documents (`/media/tip/SSD/New_Desktop/project/git/ESP32_OS/AI/help`)

#### Prohibited
- **Any** direct copying or adaptation of public code
- Third-party libraries (except during driver development phase)
- Any components or APIs of ESP-IDF (direct register manipulation only)
- Conjecturing register addresses or instruction behavior without consulting official manuals

---

### Project Structure

```
/media/tip/SSD/New_Desktop/project/git/ESP32_OS/
├── AI/
│   ├── prompt/
│   │   ├── boot/         # Boot-related technical documentation (prioritize English versions)
│   │   ├── kernel/       # Kernel-related documentation (interface understanding only, no development)
│   │   └── GUI/          # GUI-related documentation (ignore at this stage)
│   ├── AI_project/       # Plans and temporary files
│   └── help/             # Local help documents (ESP32-C6 Technical Reference Manual, etc.)
├── boot/                 # Bootloader code (current working directory)
│   ├── asm/              # Assembly source files
│   ├── src/              # C++ source files
│   └── include/          # Header files
├── kernel/               # Kernel code (to be developed; only interfaces defined at this stage)
└── drivers/              # Driver code (to be developed)
```

---

### Reference Resource Priority
1. **Highest Priority**: English documents in the `./prompt/boot/` directory (en suffix)
2. **Second Priority**: Chinese documents in the same directory (consult only when English content is difficult to understand)
3. **Technical Specifications**: ESP32-C6 Technical Reference Manual (register definitions, boot process)
4. **Conceptual Reference**: Linux kernel source code (`/media/tip/SSD/linux-7.0.1`) – algorithms and architectural ideas only

---

### Development Plan and Status Management

| Phase | Task | Status |
|-------|------|--------|
| 0 | **Environment Verification**: Check serial port devices and drivers | Must be completed before each development session |
| 1 | Hardware initialization (assembly) | Pending |
| 2 | Execution environment setup (stack, interrupt vector) | Pending |
| 3 | Serial debug output | Pending |
| 4 | Kernel probing and loading | Pending |
| 5 | API framework design | Pending |
| 6 | Boot mode management | Pending |
| 7 | Emergency mode | Pending |
| 8 | Control transfer | Pending |

#### Status Transition Rules
- **Pending → In Progress**: Update automatically when coding begins
- **In Progress → Complete**: **Must** be confirmed by me and manually updated by me

---

### Design Principles
1. **Minimalism**: Initialize only necessary devices during the boot phase; keep all non-essential peripherals disabled
2. **Modularity**: Each function is an independent module with clear interfaces for interaction
3. **Extensibility**: API design reserves extension fields to support future functionality
4. **Reliability**: Set timeouts and error handling for every critical step; emergency mode as the last line of defense
5. **Performance First**: Use assembly optimization for critical code on the boot path
6. **Originality**: All code must be original; only draw on mature design concepts from the industry
7. **Debuggability**: Output detailed debugging information at each stage to facilitate issue localization
8. **Tool use** : Allow use network toll,web search and all tools which can help you write.

---

### Pre-Development Checklist
1. **Hardware Connection**: Confirm that the ESP32-C6 development board is properly connected
2. **Serial Device**: Confirm that the serial device file exists (e.g., `/dev/ttyUSB0` or `/dev/ttyACM0`)
3. **Driver Permissions**: Confirm that the current user has read/write permissions for the serial device
4. **Debugging Tools**: Confirm that Python debugging scripts or serial terminals (e.g., `minicom`, `screen`) are available
5. **Compiler Toolchain**: Confirm that the RISC-V GCC toolchain is installed and the version is correct

---

### Workflow
1. **Environment Verification** → 2. **Requirements Analysis** (consult English documents in the boot directory) → 3. **Coding Implementation** → 4. **Self-Review** (cross-check against technical manuals) → 5. **Submit Results**

**Note**: Make all decisions independently throughout the process; no questions to me. When issues arise, consult technical manuals or local help documents.