# Self-developed Kernel Feature Enhancement Requirements Specification


## 1. Overview

This document is based on the existing self-developed kernel. All functional modules described herein shall be implemented **from the ground up**, **without relying on any off‑the‑shelf operating system frameworks** (such as ESP‑IDF, FreeRTOS, Linux kernel, etc.). All code must directly manipulate hardware or use the kernel’s internal abstraction layer, following `doc.md` as the core guideline for development.


## 2. Flash Partition Management (`part` command)

### 2.1 Functional Requirements

Implement complete flash partition management capabilities, supporting the following partition layout:

```
| boot | kernel | tools | user |
```

### 2.2 Command Specifications

| Command | Description |
|---------|-------------|
| `part -list` | List all partitions in a tree structure with detailed information |
| `part -add <partname>` | Add a new partition |
| `part -add <partname> -f <name0> -b <name1>` | Add a partition after `name0` and before `name1` (the order of `-f` and `-b` can be swapped) |
| `part -del <partname>` | Delete the specified partition |
| `part -help` | Display help information |

### 2.3 Security Constraints

- **System partition protection**: `boot` and `kernel` are considered system partitions. Deleting them requires **root privileges** verification.
- **Double confirmation mechanism**: Regardless of user role, deleting a system partition requires **two separate confirmation inputs** (type `Y` twice). Each confirmation must clearly warn about the risk of system damage.
- **Partition existence check**: When executing `-add`, if the reference partitions specified by `-f` or `-b` do not exist, the operation shall be prohibited.


## 3. Networking Stack

### 3.1 Core Networking Concepts

Implement low‑level support for the following network concepts:

| Concept | Description |
|---------|-------------|
| **IP address** | Representation, storage, and configuration of IPv4 addresses |
| **MAC address** | Reading and management of hardware addresses |
| **DHCP** | Dynamic Host Configuration Protocol client – capable of automatically obtaining IP address, subnet mask, default gateway, DNS, and other network parameters from a DHCP server |

### 3.2 Driver Implementation

- **Wi‑Fi driver**: Implement low‑level initialisation, scanning, connection, and disconnection for the Wi‑Fi module.
- **Bluetooth driver**: Implement low‑level initialisation, device discovery, pairing, and data transmission for the Bluetooth module.


## 4. MCP (Model Context Protocol) Debugging Tool

### 4.1 Positioning

Implement a **kernel‑level MCP tool** that enables AI assistants to debug the kernel through a standardised protocol. The MCP tool shall provide multiple levels of debugging capability.

### 4.2 Core Capabilities

- Expose kernel debugging interfaces via MCP tool calls.
- Support reading kernel runtime state (variables, registers, memory, etc.).
- Provide debugging features such as breakpoint setting, single‑stepping, and variable inspection.
- Integrate with AI assistants (e.g., Claude) to enable natural‑language‑driven debugging interactions.


## 5. Command System Optimisation

### 5.1 Command Merging and Refactoring

Review the existing command structure to identify overlapping or mergeable commands, following these principles:

- **Functional cohesion**: Aggregate related functionality into sub‑commands of a single parent command.
- **Consistent naming**: Unify command naming style (e.g., use `-` prefix for all parameters).
- **Reduce redundancy**: Remove commands with duplicated purposes.

### 5.2 `help` Command Simplification

The `help` command shall **display only essential commands**, for example:

```
ls, mkdir, help, part, network, command, export, ...
```

Detailed help for each command shall be obtained via its `-help` parameter.

### 5.3 `-help` Parameter Check

**Every command** must implement a `-help` parameter, outputting:

- A brief description of the command’s purpose.
- The list of available parameters and their explanations.
- Usage examples (if applicable).

### 5.4 Command Listing Management

| Command | Function | Remarks |
|---------|----------|---------|
| `command -show` | List all currently available commands | Suggested to merge into `help` or become `cmd -list` |
| `command -del <cmdname>` | Delete a third‑party command | **System built‑in commands cannot be deleted**; suggested to merge into `cmd` or rename to `unregister` |

> **Recommendation**: Merge `command -show` and `command -del` into a unified `cmd` command family, e.g., `cmd -list` and `cmd -unregister <name>`.


## 6. Environment Variables and Export

### 6.1 Environment Variable System

Implement kernel‑level environment variable management, supporting:

- **Setting** and **reading** variables.
- **Persistence** (optional).
- Variable scoping.

### 6.2 `export` Command

Inspired by Linux, the `export` command shall support:

| Usage | Description |
|-------|-------------|
| `export VAR=value` | Set and export an environment variable |
| `export VAR` | Export an existing variable to the child process environment |
| `export -n VAR` | Remove (unexport) the specified environment variable |

Exported variables shall be visible to subsequently executed commands/scripts.


## 7. Naming Conventions

- **PKI‑related commands**: Use **lowercase** consistently (e.g., `pki`).
- All command parameters should follow a uniform style; it is recommended to use the `-` prefix uniformly (e.g., `-help`, `-list`, `-add`, `-del`, etc.).


## 8. `software` Directory Implementation

Based on the prompts (hints) inside the `software` directory, implement the corresponding functionality, ensuring consistency with the overall kernel architecture.


## 9. Summary of Optimisation Principles

1. **Preserve the original intent**: All functional requirements remain unchanged; only the wording and professionalism are enhanced.
2. **Reference industry practices**: Draw on command design patterns from mature embedded systems (e.g., U‑Boot, FreeRTOS CLI, Zephyr, etc.).
3. **No arbitrary additions**: Do not introduce features or concepts not mentioned in the original text.
4. **Clear structure**: Organise by functional modules for easy step‑by‑step implementation by the development team.