#!/usr/bin/env python3
"""Flash ESP32-C6 bootloader binary to offset 0x0 via esptool.
Direct Boot mode: binary goes straight to flash[0], no IDF image header."""

import sys
import subprocess
import os

BINARY = os.path.join(os.path.dirname(__file__), "..", "bootloader.bin")
PORT = "/dev/ttyACM0"
BAUD = 460800
FLASH_OFFSET = "0x0"

def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else BINARY
    port = sys.argv[2] if len(sys.argv) > 2 else PORT

    if not os.path.exists(binary):
        print(f"Error: {binary} not found. Run 'make' first.")
        sys.exit(1)

    size = os.path.getsize(binary)
    print(f"Flashing {binary} ({size} bytes) to {port} at offset {FLASH_OFFSET}")

    cmd = [
        "esptool.py",
        "--chip", "esp32c6",
        "--port", port,
        "--baud", str(BAUD),
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        FLASH_OFFSET, binary,
    ]
    print(f"Running: {' '.join(cmd)}")
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        print("Error: esptool.py not found. Install it with: pip install esptool")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error: esptool failed with exit code {e.returncode}")
        print("Check connections and try again.")
        sys.exit(1)
    print("Flash complete. Reset the board to boot.")

if __name__ == "__main__":
    main()
