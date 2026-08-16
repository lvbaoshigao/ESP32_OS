#!/usr/bin/env python3
"""Create a password image for the ESP32-C6 bootloader.
Writes magic + SHA-256(password) to a file flashable at 0x80000 (PASSWORD_FLASH_OFFSET)."""

import hashlib
import struct
import sys

PW_MAGIC = 0xC6A55501

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <password> [output_file]")
        print(f"       {sys.argv[0]} --clear [output_file]")
        sys.exit(1)

    out = sys.argv[2] if len(sys.argv) > 2 else "password.img"

    if sys.argv[1] == "--clear":
        with open(out, "wb") as f:
            f.write(b'\xff' * 4096)
        print(f"Password cleared: {out} (flash at 0x80000)")
        return

    password = sys.argv[1].encode("utf-8")
    digest = hashlib.sha256(password).digest()

    header = struct.pack("<I", PW_MAGIC)
    data = header + digest
    # Pad to 4KB sector
    data += b'\xff' * (4096 - len(data))

    with open(out, "wb") as f:
        f.write(data)

    print(f"Password image: {out}")
    print(f"  SHA-256: {digest.hex()}")
    print(f"  Flash at: 0x80000")

if __name__ == "__main__":
    main()
