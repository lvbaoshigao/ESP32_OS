#!/usr/bin/env python3
"""Build a kernel image with header for ESP32-C6 bootloader.
Assembles the dummy kernel, prepends the header, computes CRC32."""

import struct
import subprocess
import sys
import os
import zlib

CROSS = "riscv64-unknown-elf-"
LOAD_ADDR = 0x40800000  # HP SRAM base
ENTRY_ADDR = 0x40800000  # entry = start of image
MAGIC = 0x4B36534F  # "OS6K"
HDR_SIZE = 32

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "tools/dummy_kernel.S"
    out = sys.argv[2] if len(sys.argv) > 2 else "dummy_kernel.img"

    obj = src.replace(".S", ".o")
    elf = src.replace(".S", ".elf")
    raw = src.replace(".S", ".bin")

    # Assemble
    subprocess.run([f"{CROSS}as", "-march=rv32imac", "-mabi=ilp32",
                    "-o", obj, src], check=True)

    # Link at LOAD_ADDR
    subprocess.run([f"{CROSS}ld", "-m", "elf32lriscv", "-Ttext", hex(LOAD_ADDR),
                    "-o", elf, obj], check=True)

    # Extract binary
    subprocess.run([f"{CROSS}objcopy", "-O", "binary", elf, raw], check=True)

    try:
        with open(raw, "rb") as f:
            body = f.read()

        # Pad to 4-byte alignment
        while len(body) % 4:
            body += b'\x00'

        # CRC32
        crc = zlib.crc32(body) & 0xFFFFFFFF

        # Build header — layout: magic | header_version | entry | load_addr | length | crc32 | reserved | reserved
        # Fix: corrected magic to 0x4B36534F("OS6K") and field order per spec
        header = struct.pack("<IIIIIIII",
            MAGIC,          # magic
            1,              # header_version
            ENTRY_ADDR,     # entry
            LOAD_ADDR,      # load addr
            len(body),      # size
            crc,            # checksum
            0,              # reserved
            0,              # reserved
        )
        assert len(header) == HDR_SIZE

        with open(out, "wb") as f:
            f.write(header + body)

        print(f"Kernel image: {out}")
        print(f"  Body size: {len(body)} bytes")
        print(f"  CRC32: 0x{crc:08X}")
        print(f"  Entry: 0x{ENTRY_ADDR:08X}")
        print(f"  Load:  0x{LOAD_ADDR:08X}")
        print(f"  Total: {len(header) + len(body)} bytes")
    finally:
        # Cleanup temp files — Fix: ensure cleanup even on interrupt
        for f in [obj, elf, raw]:
            try:
                os.unlink(f)
            except FileNotFoundError:
                pass

if __name__ == "__main__":
    main()
