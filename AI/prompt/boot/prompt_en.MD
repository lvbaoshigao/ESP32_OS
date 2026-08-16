### **Objective**
Develop a RISC-V assembly-compliant ESP32-C3 bootloader program that, **without relying on the IDF**, achieves the following:
1. **ROM Loading**: Loaded by the chip's built-in ROM from the SPI Flash `0x0` offset.
2. **Hardware Initialization**: Initialize necessary hardware (UART0 for debug logs, SPI Flash for subsequent loading).
3. **Debug Output**: Output detailed debug information to the specified port via UART0.
4. **Kernel Jump**: Jump to the kernel image entry located at the `0x1000` offset.

### **Development Permissions and Constraints**
1. **Permitted**:
   - Use tools like `fetch` or `wget` to retrieve online technical documents (via VPN on port `7890`).
   - Conduct web searches during development to obtain necessary technical reference information.
   - Consult local help documents in the `/media/tip/SSD/New_Desktop/project/git/ESP32_OS/AI/help` directory.
   - Use QEMU for simulation verification, but ultimately consider real hardware porting.
   - Innovative design: you may design the architecture yourself, but performance must be ensured.
   - Use plan mode.

2. **Prohibited**:
   - Making arbitrary decisions: Any operation not explicitly specified in the RISC-V assembly manual or the ESP32 Technical Reference Manual is not permitted.
   - Generating unverified code: All code must be written based on official documentation.
   - Asking me questions or seeking confirmation during development.

### **Development Environment and Toolchain**
* **Architecture**: RISC-V (ESP32-C3)
* **Assembler**: GNU Assembler (GAS), following RISC-V assembly syntax, with special attention to `.option arch` for controlling ISA extensions (e.g., compressed instruction `c`).
* **Debugging**: Use QEMU for simulation verification. For final porting, consider compatibility with real hardware; Flash mode is recommended to be **DIO/80MHz**.
* **Network**: Used only for retrieving public technical materials such as the RISC-V Instruction Set Manual and the ESP32-C3 Technical Reference Manual. The use of AI to generate unverified code or configuration is strictly prohibited.

### **ESP32-C3 Boot Process (Core Reference)**
The boot process strictly follows the path ROM -> Bootloader -> Kernel:
1. **Primary Boot (ROM)**: After chip reset, the PC points to `0x40000000` (ROM). The firmware in ROM checks `GPIO_STRAP_REG` to determine the boot mode, then reads the secondary bootloader from Flash address `0x0`, loads it into IRAM/DRAM, and finally jumps to its entry point.
2. **Secondary Boot (This Program)**: This is the program to be implemented. It is responsible for basic hardware initialization and loading the kernel from Flash address `0x1000`.
3. **Kernel Startup**: The program jumps to the kernel, which then starts advanced functions such as RTOS scheduling.

### **Implementation Process and Code Constraints**
1. **Hardware Initialization**:
   * **UART0**: Must be initialized for outputting debug information. Direct register operations should be performed by referencing the ESP32-C3 Technical Reference Manual to configure GPIO (e.g., TXD0) and the UART peripheral.
   * **SPI Flash**: Perform necessary initialization, configure it to DIO mode, and ensure correct addressing.
   * **Other Devices**: Except for the mandatory devices above, all others should remain disabled to conserve resources.
2. **Assembly Code Requirements**:
   * Use section declarations such as `.section .text.boot` to ensure code is placed in the correct memory regions.
   * Use `.global _start` to declare the entry point.
   * All interrupt handling or critical function calls must strictly adhere to the RISC-V ABI conventions (e.g., use `a0-a7` for parameter passing).
   * If needed, disable compressed instructions using `.option arch, -c`.
3. **Debugging and Logging**:
   * At key steps such as UART initialization, SPI Flash detection, segment copying, and jumping, **must** output debug information with clear stage identifiers (e.g., `[BOOT] Loader Start`) via UART0.
   * Debug information should include register values (e.g., `mhartid`) or memory addresses to assist with locating issues.
   * All debug information must be output to the specified port.

### **Simulation and Porting Process**
1. **Simulation Verification**:
   * The Espressif-maintained QEMU branch may be used for simulation.
   * Verify the correctness of the bootloader in QEMU and confirm that debug output is correct. Refer to options like `-serial pty` for redirecting output.
2. **Porting Considerations**:
   * After QEMU verification, perform secondary verification on the actual ESP32-C3 chip, paying special attention to hardware differences such as Flash timing and register address offsets.
   * The ROM's expectations for the image format (e.g., headers, segment layout) must be met; refer to the general partition table definitions.

### **Workflow**
1. **Requirements Analysis**: Strictly analyze this prompt and consult the provided help information.
2. **Information Retrieval**: If any instruction or register definition is unclear, use `fetch` or web searches to obtain the official manuals. **Guessing is prohibited**.
3. **Coding Implementation**: Write assembly code that complies with RISC-V specifications based on the documentation.
4. **Self-Review**: After writing, review each instruction's correctness against the manuals.
5. **Submission**: Organize and submit the final code along with necessary explanations. **No questions to me throughout the process; make development decisions and solve problems independently.**