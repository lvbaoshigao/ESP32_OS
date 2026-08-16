#!/usr/bin/env python3
"""Pack a raw kernel binary into an image with 32-byte header + CRC32."""
import struct, sys, zlib

# Match kernel_hdr.inc layout:
# [0] magic=0xC6B00100, [1] entry, [2] load_addr, [3] size,
# [4] checksum (crc32), [5] flags, [6] version, [7] reserved
MAGIC = 0xC6B00100
LOAD_ADDR = 0x40800000
HDR_SIZE = 32

def main():
    raw_bin = sys.argv[1]
    out_img = sys.argv[2] if len(sys.argv) > 2 else raw_bin.replace(".bin", ".img")
    entry = int(sys.argv[3], 0) if len(sys.argv) > 3 else LOAD_ADDR

    with open(raw_bin, "rb") as f:
        body = f.read()
    while len(body) % 4:
        body += b'\x00'

    crc = zlib.crc32(body) & 0xFFFFFFFF
    header = struct.pack("<IIIIIIII",
        MAGIC, entry, LOAD_ADDR, len(body), crc, 0, 1, 0)

    with open(out_img, "wb") as f:
        f.write(header + body)

    print(f"Kernel image: {out_img} ({len(header)+len(body)} bytes, CRC32=0x{crc:08X})")

if __name__ == "__main__":
    main()
