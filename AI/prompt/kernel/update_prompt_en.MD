**You are an expert operating system kernel developer. Without relying on any existing operating system frameworks** (such as ESP-IDF, FreeRTOS, Linux kernel, etc.), please update and enhance the existing custom kernel with the following functional modules. All code must be implemented from the ground up, directly manipulating hardware or using the kernel's internal abstraction layers.

---

## 1. PKI (Package Installer) Package Management Program Update

PKI is a kernel-level package management tool with the following usage:

```
PKI -help
PKI -i <package.espapp>
PKI -r <packagename>
PKI -m <file_directory>
```

### Parameter Description:
- `-help`: Display help information.
- `-i`: Install the specified `.espapp` package.
- `-r`: Remove an installed package by package name.
- `-m`: Package the specified directory into `.espapp` format.

### Implementation Requirements:
- The `.espapp` file is a custom packaging format that includes a self-describing header (package name, version, entry function offset, dependency list, file manifest, etc.).
- During installation, parse the package header, extract files to `/app/<package_name>/`, and register the package in the system command table.
- During removal, delete the corresponding directory and clean up the command table entries.
- During packaging, recursively scan the directory, generate the header, and produce the file data segment.

---

## 2. User Management

Add a `user` command to manage system user accounts.

### Usage:
```
user -add <username>
user -del <username>
user -per <username> <root|user>
```

### Parameter Description:
- `-add`: Create a new user with default permission level `user`.
- `-del`: Delete the specified user and their home directory.
- `-per`: Modify a user's permission level (`root` for superuser, `user` for ordinary user).

### Implementation Requirements:
- User data is stored in `/system/passwd` with the format `username:permission_level`.
- When creating a user, automatically generate a home directory under `/user/<username>/`, including `desktop` and `download` subdirectories.
- When deleting a user, remove all their files and directories.
- Permission checking: `root` can execute all commands; `user` is restricted (e.g., cannot install/remove packages).

---

## 3. Pipeline Identifier for Message Passing

Add a pipe mechanism to support data transfer between commands.

### Syntax:
```
command1 | command2
```

### Implementation Requirements:
- Use kernel pipe buffers (e.g., ring buffers) for inter-process communication.
- After the shell parses the `|` symbol, create a pipe, redirect the standard output of `command1` to the pipe's write end, and redirect the standard input of `command2` to the pipe's read end.
- Support multiple piped commands: `cmd1 | cmd2 | cmd3`.
- The pipe buffer size shall be configurable (default 4KB).

---

## 4. Enhanced Error Handling

Improve the system-level error handling mechanism.

### Implementation Requirements:
- Define a unified error code system (referencing POSIX errno), such as `EACCES`, `ENOENT`, `ENOMEM`, etc.
- Every system call shall return a clear error code.
- When a command fails, the shell shall display a corresponding error message, for example:
  - `PKI: File not found (ENOENT)`
  - `user: Permission denied (EACCES)`
- When a kernel panic occurs, output detailed register states and a call stack backtrace.

---

## 5. Hardware Driver Writing/Updating

Focus on updating Wi-Fi and Bluetooth drivers to achieve robust hardware support.

### Wi-Fi Driver Requirements:
- Scan nearby APs: implement the `wifisearch` command to display SSID, BSSID (MAC), channel, RSSI (dBm), and encryption type.
- View connection information: implement the `wifiinfo` command to display the currently connected SSID, MAC, signal strength, encryption type, negotiated rate, local IP, etc.
- Connect to / disconnect from a specified AP.
- Support sending raw 802.11 management frames (for security testing such as deauth).

### Bluetooth Driver Requirements:
- Scan for nearby Bluetooth devices and display device name and MAC address.
- Support BLE connections and GATT service discovery.

### Implementation Requirements:
- Use PCI bus enumeration to locate Wi-Fi / Bluetooth controllers.
- Write low-level drivers for the corresponding chips (e.g., Intel Wireless series, Atheros series).
- The network protocol stack shall either be self-implemented or a lightweight TCP/IP stack (such as a slimmed-down version of lwIP) shall be ported.

---

## 6. Add the `ps` Command

Usage is consistent with Linux:

```
ps        # Display all processes in the current terminal
ps -a     # Display all processes
ps -l     # Long format display including PID, PPID, status, priority, memory usage, command name
```

### Implementation Requirements:
- The kernel maintains a process control block (PCB) linked list, where each process includes information such as PID, status, priority, memory usage, etc.
- `ps` traverses the PCB linked list via system calls and formats the output.

---

## 7. Environment Variable Configuration

Add environment variable support so that the shell can locate executable programs based on environment variables.

### Implementation Requirements:
- Support `export VAR=value` to set environment variables.
- Support `echo $VAR` to read environment variables.
- Predefine the `PATH` environment variable with default value `/app:/system`.
- When executing a command, search for the executable file along the `PATH`.
- Environment variables are stored in the process's PCB, and child processes inherit environment variables from the parent.
- Provide the `env` command to list all environment variables.

---

## 8. File System Directory Structure

Implement the following standard directory structure:

```
/
├── system/          # System file location (kernel modules, configuration files, passwd, etc.)
├── user/            # User space location
│   ├── <username>/  # Per-user home directories
│   │   ├── desktop/ # Desktop environment location
│   │   └── download/# Default download location
│   └── ...
├── app/             # Package installation location
├── virtual/         # Virtual mount point location
├── drivers/         # Driver installation location
└── cache/           # Temporary file storage location
```

### Implementation Requirements:
- The kernel shall create the above directory structure during initialization.
- The file system must support at least basic file and directory operations (create, delete, read, write, traverse).
- Consider using FAT32 or a custom simplified file system implementation.

---

Based on the above specifications, please generate all relevant kernel C source code, header files, and shell front-end command processing code for all the modules, ensuring that the code can be independently compiled and run.