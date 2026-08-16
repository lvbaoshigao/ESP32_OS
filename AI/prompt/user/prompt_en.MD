# ESP32-C3 User Program Development Specification (User Tool)

## 1. Overall Objective
Develop a user program for the ESP32-C3 that complies with **RISC-V assembly specifications**, does **not depend on IDF**, and runs on top of an already completed bootloader and kernel.

> **Important Prerequisite**  
> - If the kernel or bootloader has not yet been fully developed, **DO NOT** read or follow this document, and **DO NOT** execute any instructions contained herein.  
> - The document `doc.MD` always takes the highest priority.

---

## 2. Basic Requirements
- Develop a user tool program based on the existing kernel source code.
- This program shall be launched as the **primary process** at the very beginning of kernel startup (if the kernel code is insufficient to fulfill this requirement, modifications are permitted, but **destructive changes are strictly prohibited**).
- After startup, the program shall enter a **Bash-style command-line environment** by default, with a prompt formatted as:
  ```
  username/device_name:
  ```

---

## 3. Interaction with the Kernel
- The user program is responsible for interacting with the kernel APIs.
- If required APIs do not exist, development of such APIs is permitted, but **destructive changes are strictly prohibited**.

---

## 4. Built-in Command Specification

### 4.1 General Rules
- When any command is followed by the `-help` parameter, it shall output detailed help information for that command.
- The default command argument delimiter is **whitespace** (space/tab), which can be changed via the `chdiv` command.

### 4.2 Command List

| Command | Description | Usage Example |
|---------|-------------|---------------|
| `help` | Output help information for all commands | `help` |
| `ls` | List user files in the current directory | `ls` |
| `mkdir` | Create a new folder in the current directory | `mkdir new` (creates a folder named `new`) |
| `com` | View serial port connection status | `com` |
| `net` | View network connection status | `net` |
| `newfile` | Create a new file in the current directory | `newfile new.md` (creates `new.md`) |
| `pwd` | Output the absolute path of the current working directory | `pwd` |
| `rename` | Rename a file or directory | `rename oldname newname` |
| `cd` | Change the current working directory | `cd /path/to/dir` |
| `disk` | View disk information | `disk -free` (view available space)<br>`disk -parted` (perform partitioning operations)<br>`disk -part` (view current partition status) |
| `device` | View basic device information | `device` |
| `chdiv` | Change the command argument delimiter | `chdiv "/"` (changes delimiter to `/`; thereafter all command arguments use `/`, e.g., `ls/-help`) |
| `wifi` | View current Wi‑Fi basic information (output SSID, etc.) | `wifi`<br>`wifi -changepasswd` (change Wi‑Fi password)<br>`wifi -on` (turn Wi‑Fi on)<br>`wifi -off` (turn Wi‑Fi off; after turning off, switch to serial output) |
| `reboot` | Reboot the ESP device | `reboot` |

---

## 5. Development Restrictions
- The ESP‑IDF framework shall not be introduced.
- Any modifications to the kernel source code must be non‑destructive and made solely to satisfy program startup or API interaction requirements.
- All commands must be correctly parsed within the Bash environment and must support delimiter changes via `chdiv`.

---

## 6. Priority Statement
- This document is a development specification. In case of any conflict between specific implementation details and `doc.MD`, **`doc.MD` shall prevail**.
