# ESP32-C6 Kernel Image Header Format
# Placed at flash offset 0x1000 (mapped to 0x4200_1000 in Direct Boot)
#
# All fields are little-endian 32-bit words

.equ KERNEL_FLASH_OFFSET,   0x1000
.equ KERNEL_FLASH_ADDR,     0x42001000   # flash base + offset via cache

# Header structure (32 bytes)
.equ KERN_HDR_MAGIC,         0x00   # Magic: 0xC6B00100 ("C6 Boot v1.00")
.equ KERN_HDR_ENTRY,         0x04   # Entry point address (RAM address after copy)
.equ KERN_HDR_LOAD_ADDR,     0x08   # RAM load address (where to copy)
.equ KERN_HDR_SIZE,          0x0C   # Image size in bytes (excluding header)
.equ KERN_HDR_CHECKSUM,      0x10   # CRC32 of image body (after header)
.equ KERN_HDR_FLAGS,         0x14   # Flags (reserved, set to 0)
.equ KERN_HDR_VERSION,       0x18   # Kernel version (user-defined)
.equ KERN_HDR_RESERVED,      0x1C   # Reserved (must be 0)
.equ KERN_HDR_LEN,           0x20   # Total header length = 32 bytes

.equ KERNEL_MAGIC,           0xC6B00100

# Maximum kernel size: 480KB (leave 32KB for bootloader stack/data)
.equ KERNEL_MAX_SIZE,        (480 * 1024)
