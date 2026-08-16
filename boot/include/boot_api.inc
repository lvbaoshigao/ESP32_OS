# ESP32-C6 Boot API — Bootloader <-> Kernel Interface
# Passed to kernel in a0 as pointer to boot_params_t in SRAM

# === Boot Parameters (passed to kernel via a0) ===
# Located at a fixed address in top of SRAM so kernel can always find it
.equ BOOT_PARAMS_ADDR,   0x4087F000   # 4KB below stack top

# boot_params_t layout (64 bytes, all 32-bit LE words)
.equ BP_MAGIC,            0x00   # 0xC6BOOT01 — validity marker
.equ BP_API_VERSION,      0x04   # API version (1.0 = 0x00010000)
.equ BP_BOOT_MODE,        0x08   # Boot mode (see BOOT_MODE_* below)
.equ BP_BOOT_SOURCE,      0x0C   # Boot source (see BOOT_SRC_* below)
.equ BP_KERN_ENTRY,       0x10   # Kernel entry point used
.equ BP_KERN_LOAD,        0x14   # Kernel load address
.equ BP_KERN_SIZE,        0x18   # Kernel image size
.equ BP_HW_CRYSTAL,      0x1C   # Crystal frequency in Hz (40000000)
.equ BP_HW_SRAM_SIZE,    0x20   # HP SRAM size in bytes (512K)
.equ BP_HW_CHIP_REV,     0x24   # Chip revision (from eFuse)
.equ BP_RETRY_COUNT,     0x28   # Boot retry counter
.equ BP_FLAGS,            0x2C   # Flags (reserved)
.equ BP_RESERVED,         0x30   # Reserved (4 words = 16 bytes for extension)
.equ BP_SIZE,             0x40   # Total: 64 bytes

.equ BP_MAGIC_VALUE,      0xC6B00101

# API version: major.minor in 16.16 format
.equ BP_API_VER_1_0,      0x00010000

# === Boot Modes ===
.equ BOOT_MODE_NORMAL,    0x00
.equ BOOT_MODE_SAFE,      0x01
.equ BOOT_MODE_RECOVERY,  0x02
.equ BOOT_MODE_FACTORY,   0x03

# === Boot Sources ===
.equ BOOT_SRC_MAIN_FLASH, 0x00
.equ BOOT_SRC_BACKUP,     0x01   # ponytail: defined but not implemented yet
.equ BOOT_SRC_EXTERNAL,   0x02   # ponytail: defined but not implemented yet

# === Kernel -> Bootloader Mailbox ===
# Fixed address in SRAM, survives a warm reset if LP_SRAM used
# Using HP SRAM for now (simpler, cleared on chip reset)
.equ MAILBOX_ADDR,        0x4087F800   # 2KB above boot params
.equ MAILBOX_SIZE,        0x40         # 64 bytes

# mailbox_t layout
.equ MB_MAGIC,            0x00   # 0xC6MAIL01 — kernel wrote this
.equ MB_STATUS,           0x04   # Kernel status code
.equ MB_REQUEST,          0x08   # Mode switch request (BOOT_MODE_*)
.equ MB_RETRY,            0x0C   # Retry counter (kernel increments on failure)
.equ MB_FLAGS,            0x10   # Flags
.equ MB_RESERVED,         0x14   # Reserved (11 words)

.equ MB_MAGIC_VALUE,      0xC6AABB01

# Kernel status codes
.equ KERN_STATUS_OK,      0x00
.equ KERN_STATUS_FAIL,    0x01
.equ KERN_STATUS_PANIC,   0x02
.equ KERN_STATUS_REBOOT,  0x03

# === Password API ===
# Password hash stored in flash at offset 0x80000 (512KB, past kernel max range)
.equ PASSWORD_FLASH_OFFSET, 0x80000
.equ PASSWORD_FLASH_ADDR,   0x42080000   # cache-mapped read address
.equ PASSWORD_SECTOR,       0x80000      # sector address for erase

# Password slot layout in flash (36 bytes)
.equ PW_MAGIC,             0x00          # 0xC6PA5501 if password set
.equ PW_HASH,              0x04          # 32 bytes SHA-256 hash
.equ PW_SLOT_SIZE,         0x24          # 36 bytes total

.equ PW_MAGIC_VALUE,       0xC6A55501
.equ PW_MAX_INPUT,         55            # max password length (single SHA block limit)
.equ PW_MAX_ATTEMPTS,      3

# Mailbox password commands (kernel sets MB_REQUEST to these)
.equ MB_CMD_SET_PASSWORD,  0x10          # kernel provides new SHA256 hash
.equ MB_CMD_DEL_PASSWORD,  0x11          # delete password

# Mailbox extended data area for password hash (follows standard mailbox)
.equ MB_PW_HASH,           0x20          # offset in mailbox: 32 bytes for SHA256
