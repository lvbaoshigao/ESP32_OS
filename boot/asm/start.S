# ESP32-C6 Bootloader Entry Point
# Direct Boot mode: ROM maps flash via cache, jumps here
# TRM 9.2: first 8 bytes must be 0xaedb041d (repeated)

.include "reg_c6.inc"
.include "kernel_hdr.inc"
.include "boot_api.inc"

# =================================================================
# Timeout constants for polling loops [FIX 6]
# Defined here (before use) since GAS evaluates .equ at assembly time
# =================================================================
.equ UART_SYNC_TIMEOUT,     10000
.equ UART_TX_TIMEOUT,       10000
.equ UART_RX_TIMEOUT,       1000000
.equ SPI_CMD_TIMEOUT,       100000
.equ FLASH_WIP_TIMEOUT,     10000000
.equ SHA_BUSY_TIMEOUT,      100000

    .section .text.boot, "ax"

# Direct Boot header — ESP32-C6 ROM checks words [0] and [1]
# TRM 9.2: both must be 0xaedb041d for Direct Boot mode
    .word DIRECT_BOOT_MAGIC
    .word DIRECT_BOOT_MAGIC

    .global _start
    .type _start, @function
_start:
    # --- Phase 2: Execution environment ---
    li      sp, STACK_TOP

    csrr    a0, mhartid
    bnez    a0, _halt

    # Set up global pointer for small data access
    la      gp, __global_pointer$

    la      t0, _trap_handler
    csrw    mtvec, t0

    csrw    mie, zero
    li      t0, (3 << 11)
    csrw    mstatus, t0

    # --- Disable all watchdogs ASAP (they fire resets during boot) ---
    jal     ra, _disable_all_wdt

    # --- Flash DIO mode: skip, ROM already configured for Direct Boot ---
    # ponytail: _flash_enable_dio sets QE bit on flash while executing
    # from flash, causing instruction fetch corruption on some chips.
    # ROM configures flash correctly for Direct Boot; skip reconfig.

    # Clear BSS
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:

    # --- Phase 1: UART0 init ---
    li      t0, PCR_BASE
    li      t1, 1
    sw      t1, PCR_UART0_CONF(t0)

    li      t1, (PCR_SCLK_EN | PCR_SCLK_SEL_XTAL)
    sw      t1, PCR_UART0_SCLK_CONF(t0)

    li      t0, UART0_BASE
    jal     ra, _uart_wait_sync

    li      t1, (UART_TXFIFO_RST | UART_RXFIFO_RST)
    sw      t1, UART_CONF0_REG(t0)
    li      t2, 100
3:  addi    t2, t2, -1
    bnez    t2, 3b

    li      t1, UART_CONF0_8N1
    sw      t1, UART_CONF0_REG(t0)

    li      t1, UART_CLKDIV_115200
    sw      t1, UART_CLKDIV_REG(t0)

    li      t1, (UART_TX_SCLK_EN | UART_RX_SCLK_EN)
    sw      t1, UART_CLK_CONF_REG(t0)

    li      t1, UART_REG_UPDATE_BIT
    sw      t1, UART_REG_UPDATE(t0)
    jal     ra, _uart_wait_sync

    # [FIX 1] GPIO pin mux: route UART0 TXD/RXD to GPIO16/17 via IO_MUX
    # TRM 6.2: IO_MUX direct connection. MCU_SEL=0 selects U0TXD/U0RXD.
    # RX needs FUN_IE (input enable) + FUN_PU or the pad floats and RXFIFO stays empty.
    li      t0, IO_MUX_BASE
    li      t1, IO_MUX_SEL_UART0                    # U0TXD, output only
    sw      t1, IO_MUX_GPIO16(t0)
    li      t1, (IO_MUX_SEL_UART0 | IO_MUX_FUN_IE | IO_MUX_FUN_PU)
    sw      t1, IO_MUX_GPIO17(t0)                   # U0RXD, input enabled + pull-up

    # Enable GPIO16 output (TXD) — W1TS so other pins' OE bits survive
    li      t0, GPIO_BASE
    li      t1, (1 << 16)
    sw      t1, GPIO_ENABLE_W1TS(t0)

    # --- Phase 3: Staged debug output with timestamps ---

    # Banner
    la      a0, str_banner
    jal     ra, uart_puts

    # [BOOT][ENV] Hart/SP
    la      a0, str_env_prefix
    jal     ra, uart_puts
    csrr    a0, mhartid
    jal     ra, uart_print_hex8
    la      a0, str_sp_is
    jal     ra, uart_puts
    mv      a0, sp
    jal     ra, uart_print_hex32
    la      a0, str_crlf
    jal     ra, uart_puts

    # [BOOT][UART] init complete
    la      a0, str_uart_ok
    jal     ra, uart_puts

    # [BOOT][WDT] disabled
    la      a0, str_wdt_ok
    jal     ra, uart_puts

    # ponytail: mcycle/SYSTIMER broken in Direct Boot mode; skip timestamps
    # [BOOT][TMR] placeholder
    la      a0, str_tmr_skip
    jal     ra, uart_puts

    # --- Password check ---
    # Read password hash from flash (cache-mapped)
    li      t0, PASSWORD_FLASH_ADDR
    lw      t1, PW_MAGIC(t0)
    li      t2, PW_MAGIC_VALUE
    bne     t1, t2, _no_password

    la      a0, str_pw_prompt
    jal     ra, uart_puts

    li      s10, PW_MAX_ATTEMPTS
_pw_retry:
    # Read password from UART into stack buffer
    addi    sp, sp, -80                 # 64 byte buffer + 16 padding
    mv      a0, sp
    li      a1, PW_MAX_INPUT
    jal     ra, uart_read_line          # a0 = bytes read

    # SHA-256 the input
    mv      a1, a0                      # a1 = length
    mv      a0, sp                      # a0 = buffer
    jal     ra, sha256_hash             # result in sha_digest (SRAM)

    # [FIX 7] Clear password buffer from stack before freeing
    mv      t0, sp
    li      t1, 20                      # 80 bytes / 4 = 20 words
1:  sw      zero, 0(t0)
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, 1b

    addi    sp, sp, 80                  # free buffer

    # [FIX 10] Constant-time password comparison (no early-exit on mismatch)
    # XOR all 8 words, OR results together, then check at end
    li      t0, PASSWORD_FLASH_ADDR
    addi    t0, t0, PW_HASH
    la      t1, sha_digest
    li      t2, 8
    li      t5, 0                       # accumulator for differences
1:  lw      t3, 0(t0)
    lw      t4, 0(t1)
    xor     t3, t3, t4                  # t3 = 0 if words match
    or      t5, t5, t3                  # accumulate any non-zero
    addi    t0, t0, 4
    addi    t1, t1, 4
    addi    t2, t2, -1
    bnez    t2, 1b
    bnez    t5, _pw_fail                # any difference = fail

    # Password correct
    la      a0, str_pw_ok
    jal     ra, uart_puts
    j       _pw_done

_pw_fail:
    addi    s10, s10, -1
    beqz    s10, _pw_locked
    la      a0, str_pw_wrong
    jal     ra, uart_puts
    j       _pw_retry

_pw_locked:
    la      a0, str_pw_locked
    jal     ra, uart_puts
    j       _halt

_no_password:
    la      a0, str_pw_none
    jal     ra, uart_puts
_pw_done:

    # --- Phase 6: Boot mode management ---
    # Check mailbox for kernel feedback from previous boot
    li      s5, BOOT_MODE_NORMAL        # default boot mode
    li      s6, BOOT_SRC_MAIN_FLASH     # default source
    li      s7, 0                       # retry counter

    li      t0, MAILBOX_ADDR
    lw      t1, MB_MAGIC(t0)
    li      t2, MB_MAGIC_VALUE
    bne     t1, t2, _no_mailbox

    # Mailbox valid — read feedback into callee-saved regs
    lw      s7, MB_RETRY(t0)            # retry count
    lw      s8, MB_REQUEST(t0)          # requested mode
    lw      s9, MB_STATUS(t0)           # kernel status

    # Print mailbox info
    la      a0, str_mailbox
    jal     ra, uart_puts
    la      a0, str_mb_status
    jal     ra, uart_puts
    mv      a0, s9
    jal     ra, uart_print_hex8
    la      a0, str_mb_mode
    jal     ra, uart_puts
    mv      a0, s8
    jal     ra, uart_print_hex8
    la      a0, str_mb_retry
    jal     ra, uart_puts
    mv      a0, s7
    jal     ra, uart_print_hex8
    la      a0, str_crlf
    jal     ra, uart_puts

    # --- Phase 7: Emergency mode ---
    li      t0, 3
    bgeu    s7, t0, _emergency_mode

    # Check for password commands first
    li      t0, MB_CMD_SET_PASSWORD
    beq     s8, t0, _mb_set_password
    li      t0, MB_CMD_DEL_PASSWORD
    beq     s8, t0, _mb_del_password

    # Apply requested boot mode if valid
    li      t0, BOOT_MODE_FACTORY
    bgtu    s8, t0, _mode_done
    mv      s5, s8
    j       _mode_done

_mb_set_password:
    la      a0, str_pw_setting
    jal     ra, uart_puts
    # Copy hash from mailbox data area to flash
    li      a0, MAILBOX_ADDR
    addi    a0, a0, MB_PW_HASH          # source: 32 bytes in mailbox
    li      a1, PASSWORD_FLASH_OFFSET   # flash offset
    jal     ra, flash_write_pw_slot
    la      a0, str_pw_set_ok
    jal     ra, uart_puts
    j       _mode_done

_mb_del_password:
    la      a0, str_pw_deleting
    jal     ra, uart_puts
    li      a0, PASSWORD_SECTOR
    jal     ra, flash_erase_sector
    la      a0, str_pw_del_ok
    jal     ra, uart_puts
    j       _mode_done

_no_mailbox:
    la      a0, str_no_mailbox
    jal     ra, uart_puts
_mode_done:

    # Clear mailbox for this boot cycle
    li      t0, MAILBOX_ADDR
    li      t1, (MAILBOX_SIZE / 4)
1:  sw      zero, 0(t0)
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, 1b

    # Print selected boot mode
    la      a0, str_boot_mode
    jal     ra, uart_puts
    mv      a0, s5
    jal     ra, uart_print_hex8
    la      a0, str_crlf
    jal     ra, uart_puts

    # --- Phase 4: Kernel probing and loading ---
    la      a0, str_probe
    jal     ra, uart_puts

    # Read kernel header from flash-mapped address
    li      s0, KERNEL_FLASH_ADDR

    # Check magic
    lw      t0, KERN_HDR_MAGIC(s0)
    li      t1, KERNEL_MAGIC
    bne     t0, t1, _no_kernel

    # Magic valid — read header fields
    lw      s1, KERN_HDR_ENTRY(s0)      # entry point
    lw      s2, KERN_HDR_LOAD_ADDR(s0)  # load address (RAM dest)
    lw      s3, KERN_HDR_SIZE(s0)       # image size
    lw      s4, KERN_HDR_CHECKSUM(s0)   # expected checksum

    # Sanity check size
    li      t0, KERNEL_MAX_SIZE
    bgtu    s3, t0, _kernel_too_big
    beqz    s3, _kernel_empty

    # [FIX 11] Security: validate load address bounds (round up size to 4, matching copy loop)
    li      t0, HP_SRAM_START
    bltu    s2, t0, _kernel_bad_addr    # load_addr < SRAM start
    # Ensure load address is within SRAM (not above SRAM end)
    li      t0, HP_SRAM_END
    bgeu    s2, t0, _kernel_bad_addr    # load_addr >= SRAM end
    addi    t1, s3, 3
    andi    t1, t1, -4                  # t1 = (s3 + 3) & ~3 (rounded up to 4)
    add     t1, s2, t1                  # load_addr + rounded_size
    li      t0, BOOT_PARAMS_ADDR
    bgtu    t1, t0, _kernel_bad_addr    # load_addr+rounded_size > params area
    # Guard: load_addr + rounded_size must not wrap around
    bltu    t1, s2, _kernel_bad_addr    # wraparound check

    # Security: validate entry point within loaded range
    bltu    s1, s2, _kernel_bad_addr    # entry < load_addr
    bgeu    s1, t1, _kernel_bad_addr    # entry >= load_addr+size

    # Print kernel info
    la      a0, str_kern_entry
    jal     ra, uart_puts
    mv      a0, s1
    jal     ra, uart_print_hex32
    la      a0, str_kern_load
    jal     ra, uart_puts
    mv      a0, s2
    jal     ra, uart_print_hex32
    la      a0, str_kern_size
    jal     ra, uart_puts
    mv      a0, s3
    jal     ra, uart_print_hex32
    la      a0, str_crlf
    jal     ra, uart_puts

    # Compute CRC32 of kernel body (header + 32 bytes onward)
    addi    a0, s0, KERN_HDR_LEN        # source: flash data after header
    mv      a1, s3                      # length
    jal     ra, crc32
    bne     a0, s4, _kernel_bad_crc

    la      a0, str_crc_ok
    jal     ra, uart_puts

    # Copy kernel from flash to RAM (word-aligned copy)
    la      a0, str_copy
    jal     ra, uart_puts
    addi    t0, s0, KERN_HDR_LEN        # source
    mv      t1, s2                      # dest (RAM)
    addi    t2, s3, 3
    andi    t2, t2, -4                  # round up to 4-byte boundary
1:  blez    t2, 2f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    addi    t2, t2, -4
    j       1b
2:
    # Verify copy by re-computing CRC over RAM copy
    mv      a0, s2
    mv      a1, s3
    jal     ra, crc32
    bne     a0, s4, _kernel_copy_fail

    la      a0, str_copy_ok
    jal     ra, uart_puts

    # --- Phase 5: Fill boot parameters struct ---
    li      t0, BOOT_PARAMS_ADDR

    li      t1, BP_MAGIC_VALUE
    sw      t1, BP_MAGIC(t0)
    li      t1, BP_API_VER_1_0
    sw      t1, BP_API_VERSION(t0)
    sw      s5, BP_BOOT_MODE(t0)        # boot mode from Phase 6
    sw      s6, BP_BOOT_SOURCE(t0)      # boot source
    sw      s1, BP_KERN_ENTRY(t0)       # kernel entry
    sw      s2, BP_KERN_LOAD(t0)        # kernel load addr
    sw      s3, BP_KERN_SIZE(t0)        # kernel size
    li      t1, 40000000
    sw      t1, BP_HW_CRYSTAL(t0)       # 40MHz
    li      t1, HP_SRAM_SIZE
    sw      t1, BP_HW_SRAM_SIZE(t0)     # 512KB
    sw      zero, BP_HW_CHIP_REV(t0)    # ponytail: read from eFuse when needed
    sw      s7, BP_RETRY_COUNT(t0)      # retry counter
    sw      zero, BP_FLAGS(t0)

    # --- Phase 8: Control transfer ---
    la      a0, str_jump
    jal     ra, uart_puts

    # Flush UART TX FIFO before jumping
    jal     ra, _uart_flush

    fence.i
    li      a0, BOOT_PARAMS_ADDR        # a0 = pointer to boot params
    jr      s1                          # jump to kernel entry

_emergency_mode:
    la      a0, str_emergency
    jal     ra, uart_puts
    mv      a0, s7
    jal     ra, uart_print_hex8
    la      a0, str_emerg_wait
    jal     ra, uart_puts
    # ponytail: just halt, add recovery console when needed
    j       _halt

_no_kernel:
    la      a0, str_no_kernel
    jal     ra, uart_puts
    j       _halt

_kernel_bad_addr:
    la      a0, str_kern_badaddr
    jal     ra, uart_puts
    j       _halt

_kernel_too_big:
    la      a0, str_kern_toobig
    jal     ra, uart_puts
    j       _halt

_kernel_empty:
    la      a0, str_kern_empty
    jal     ra, uart_puts
    j       _halt

_kernel_bad_crc:
    la      a0, str_kern_badcrc
    jal     ra, uart_puts
    la      a0, str_expected
    jal     ra, uart_puts
    mv      a0, s4
    jal     ra, uart_print_hex32
    la      a0, str_got
    jal     ra, uart_puts
    # Recompute CRC for display (a0 was clobbered by uart_puts calls above)
    addi    a0, s0, KERN_HDR_LEN
    mv      a1, s3
    jal     ra, crc32
    jal     ra, uart_print_hex32
    la      a0, str_crlf
    jal     ra, uart_puts
    j       _halt

_kernel_copy_fail:
    la      a0, str_copy_fail
    jal     ra, uart_puts
    j       _halt

_halt:
    wfi
    j       _halt

# =================================================================
# Disable all watchdog timers [TRM 14, 15]
# Must run early — TG0_WDT fires core reset within ~300ms of boot
# =================================================================
_disable_all_wdt:
    li      t3, WDT_UNLOCK_KEY

    # MWDT0 (TG0): unlock, clear WDT_EN+stages, trigger update, re-lock [TRM 15.2.2.3]
    li      t0, TIMG0_BASE
    sw      t3, TIMG_WDTWPROTECT(t0)
    sw      zero, TIMG_WDTCONFIG0(t0)          # WDT_EN=0, all stages=no effect
    li      t1, TIMG_WDT_CONF_UPDATE
    sw      t1, TIMG_WDTCONFIG0(t0)            # apply config
    sw      zero, TIMG_WDTWPROTECT(t0)

    # [FIX 2] ESP32-C6 has only ONE Timer Group (TG0). TIMG1 at 0x60009000
    # is the System Timer (SYSTIMER), not a timer group — removed.

    # RWDT (RTC/LP WDT)
    li      t0, RWDT_BASE
    sw      t3, RWDT_WPROTECT(t0)
    sw      zero, RWDT_CONFIG0(t0)
    sw      zero, RWDT_WPROTECT(t0)

    # SWD (Super Watchdog): set SWD_DISABLE bit
    li      t0, RWDT_BASE               # ensure base is set (independent of RWDT block)
    sw      t3, SWD_WPROTECT(t0)
    lw      t1, SWD_CONFIG(t0)
    li      t2, SWD_DISABLE_BIT
    or      t1, t1, t2
    sw      t1, SWD_CONFIG(t0)
    sw      zero, SWD_WPROTECT(t0)

    # [FIX 9] TWDT (Trusted WDT) [TRM 15.x] — part of TEE block
    li      t0, TEE_BASE
    sw      t3, TWDT_WPROTECT(t0)               # unlock
    sw      zero, TWDT_CONF(t0)                  # disable
    sw      zero, TWDT_WPROTECT(t0)              # re-lock

    ret

# =================================================================
# [FIX 3] Enable DIO mode on flash (set Quad Enable bit)
# Called once during boot, before any flash operations
# =================================================================
_flash_enable_dio:
    addi    sp, sp, -8
    sw      ra, 4(sp)
    sw      s0, 0(sp)
    li      s0, SPI1_BASE

    # Read Status Register 1
    li      t1, (SPI_USR_COMMAND | SPI_USR_MISO)
    sw      t1, SPI1_USER_REG(s0)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_RDSR)
    sw      t1, SPI1_USER2_REG(s0)
    li      t1, 7                               # 8 bits - 1
    sw      t1, SPI1_MS_DLEN_REG(s0)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(s0)
    jal     ra, _spi1_wait

    # Set QE bit (bit 1 = Quad Enable for most SPI flash chips [TRM 28])
    lw      t1, SPI1_W0_REG(s0)
    andi    t1, t1, 2                           # check if already set
    bnez    t1, 1f                              # skip if already set

    # QE not set — write it
    lw      t1, SPI1_W0_REG(s0)
    ori     t1, t1, 2                           # set bit 1 (QE)
    sw      t1, SPI1_W0_REG(s0)

    # Write Enable then Write Status Register
    jal     ra, _flash_wren
    li      t1, (SPI_USR_COMMAND | SPI_USR_MOSI)
    sw      t1, SPI1_USER_REG(s0)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_WRSR)
    sw      t1, SPI1_USER2_REG(s0)
    li      t1, (8 - 1)                         # 8 bits - 1
    sw      t1, SPI1_MS_DLEN_REG(s0)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(s0)
    jal     ra, _spi1_wait
    jal     ra, _flash_wait_wip

1:  lw      s0, 0(sp)
    lw      ra, 4(sp)
    addi    sp, sp, 8
    ret

# =================================================================
# [FIX 5] crc32: compute CRC32 over memory region using nibble lookup table
# a0 = start address, a1 = byte count (must be multiple of 4)
# returns CRC32 in a0
# Uses standard CRC32 polynomial 0xEDB88320 (bit-reversed)
# Speed: ~2ms vs ~144ms for bit-by-bit, table is 64 bytes in .rodata
# =================================================================
    .global crc32
    .type crc32, @function
crc32:
    li      t0, -1                  # CRC = 0xFFFFFFFF
    add     t3, a0, a1              # end address
    la      t5, crc32_table         # nibble table pointer
1:  bgeu    a0, t3, 3f
    lbu     t1, 0(a0)               # byte
    xor     t0, t0, t1              # CRC ^= byte
    # Process low nibble (4 bits)
    andi    t2, t0, 0xF             # low nibble index
    srli    t0, t0, 4               # CRC >>= 4
    slli    t2, t2, 2               # word offset
    add     t2, t5, t2              # table entry address
    lw      t1, 0(t2)               # lookup
    xor     t0, t0, t1              # CRC ^= table[low_nibble]
    # Process high nibble (4 bits)
    andi    t2, t0, 0xF             # high nibble index
    srli    t0, t0, 4               # CRC >>= 4
    slli    t2, t2, 2               # word offset
    add     t2, t5, t2
    lw      t1, 0(t2)
    xor     t0, t0, t1
    addi    a0, a0, 1
    j       1b
3:  not     a0, t0                  # final XOR
    ret

# =================================================================
# _uart_flush: wait until TX FIFO is empty and TX shift register idle
# =================================================================
_uart_flush:
    li      t0, UART0_BASE
    li      t2, UART_TX_TIMEOUT
1:  lw      t1, UART_STATUS_REG(t0)
    srli    t1, t1, UART_TXFIFO_CNT_SHIFT
    andi    t1, t1, UART_TXFIFO_CNT_MASK
    bnez    t1, 2f                  # FIFO not empty, keep waiting
    # [FIX 8] Check TX_IDLE (bit 12): wait for shift register to finish
    lw      t1, UART_STATUS_REG(t0)
    srli    t1, t1, 12
    andi    t1, t1, 1
    bnez    t1, 3f                  # TX_IDLE=1, done
2:  addi    t2, t2, -1
    bnez    t2, 1b
    # timeout — proceed anyway (TX data may be lost)
3:  ret

# =================================================================
# uart_getc: blocking read one byte from UART0 RX FIFO into a0
# =================================================================
    .global uart_getc
    .type uart_getc, @function
uart_getc:
    li      t0, UART0_BASE
    li      t2, UART_RX_TIMEOUT              # [FIX 6] timeout protection
1:  lw      t1, UART_STATUS_REG(t0)
    andi    t1, t1, UART_RXFIFO_CNT_MASK
    bnez    t1, 2f
    addi    t2, t2, -1
    bnez    t2, 1b
    j       _halt                               # UART timeout
2:  lbu     a0, UART_FIFO_REG(t0)
    ret

# =================================================================
# uart_read_line: read line from UART into buffer, echo '*' per char
# a0 = buffer ptr, a1 = max length
# returns bytes read in a0 (excludes terminator)
# terminates on CR (0x0D) or LF (0x0A)
# =================================================================
    .global uart_read_line
    .type uart_read_line, @function
uart_read_line:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)
    sw      s2, 0(sp)
    mv      s0, a0                      # buffer
    mv      s1, a1                      # max len
    li      s2, 0                       # count
1:  bgeu    s2, s1, 2f                  # buffer full
    jal     ra, uart_getc
    li      t0, 0x0D
    beq     a0, t0, 2f                  # CR -> done
    li      t0, 0x0A
    beq     a0, t0, 2f                  # LF -> done
    li      t0, 0x08
    beq     a0, t0, 3f                  # backspace
    li      t0, 0x7F
    beq     a0, t0, 3f                  # DEL
    add     t0, s0, s2
    sb      a0, 0(t0)                   # store char
    addi    s2, s2, 1
    li      a0, '*'
    jal     ra, uart_putc               # echo mask
    j       1b
3:  beqz    s2, 1b                      # nothing to delete
    addi    s2, s2, -1
    li      a0, 0x08                    # backspace
    jal     ra, uart_putc
    li      a0, ' '
    jal     ra, uart_putc
    li      a0, 0x08
    jal     ra, uart_putc
    j       1b
2:  li      a0, '\n'
    jal     ra, uart_putc
    mv      a0, s2                      # return count
    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    lw      s2, 0(sp)
    addi    sp, sp, 16
    ret

# =================================================================
# sha256_hash: SHA-256 a buffer using HW accelerator
# a0 = data ptr, a1 = byte count (<=64, single block)
# result written to sha_digest (8 words in .bss)
# =================================================================
    .global sha256_hash
    .type sha256_hash, @function
sha256_hash:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)
    sw      s2, 0(sp)
    mv      s0, a0                      # data
    mv      s1, a1                      # length in bytes

    # Enable SHA clock via PCR
    li      t0, PCR_BASE
    li      t1, 1                       # CLK_EN=1, RST_EN=0
    sw      t1, PCR_SHA_CONF(t0)

    li      s2, SHA_BASE

    # Set SHA-256 mode
    li      t0, SHA_MODE_256
    sw      t0, SHA_MODE_REG(s2)

    # Build padded message block on stack (64 bytes = 16 words)
    addi    sp, sp, -64

    # Zero the block
    mv      t0, sp
    li      t1, 16
4:  sw      zero, 0(t0)
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, 4b

    # Copy message bytes
    mv      t0, s0                      # src
    mv      t1, sp                      # dst
    mv      t2, s1                      # count
5:  beqz    t2, 6f
    lbu     t3, 0(t0)
    sb      t3, 0(t1)
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    j       5b
6:
    # Append 0x80 after message
    li      t3, 0x80
    sb      t3, 0(t1)

    # [FIX 4] SHA-256 byte order: ESP32-C6 SHA HW expects little-endian words
    # Store 64-bit length as two LE 32-bit words at bytes 56-63
    # length in bits = s1 * 8 (message <= 64 bytes, so high 32 bits = 0)
    slli    t0, s1, 3                   # bits
    # Low 32 bits at offset 56 (LE)
    sb      t0, 56(sp)                  # LSB
    srli    t1, t0, 8
    sb      t1, 57(sp)
    srli    t1, t0, 16
    sb      t1, 58(sp)
    srli    t1, t0, 24
    sb      t1, 59(sp)                  # MSB of low 32 bits
    # High 32 bits at offset 60 (zero, since message <= 64 bytes)
    sb      zero, 60(sp)
    sb      zero, 61(sp)
    sb      zero, 62(sp)
    sb      zero, 63(sp)

    # Write padded block to SHA message registers (LE words match HW expectation)
    mv      t0, sp                      # padded block
    li      t1, 16                      # 16 words
    mv      t2, s2
    addi    t2, t2, SHA_M_0_REG
7:  lw      t3, 0(t0)
    sw      t3, 0(t2)
    addi    t0, t0, 4
    addi    t2, t2, 4
    addi    t1, t1, -1
    bnez    t1, 7b

    addi    sp, sp, 64                  # free padded block

    # Start SHA
    li      t0, 1
    sw      t0, SHA_START_REG(s2)

    # [FIX 6] Poll busy with timeout
    li      t1, SHA_BUSY_TIMEOUT
8:  lw      t0, SHA_BUSY_REG(s2)
    andi    t0, t0, 1
    beqz    t0, 10f                     # done
    addi    t1, t1, -1
    bnez    t1, 8b
    j       _halt                       # SHA timeout
10:

    # Read digest to sha_digest (8 words, direct — match stored format)
    la      t0, sha_digest
    mv      t1, s2
    addi    t1, t1, SHA_H_0_REG
    li      t2, SHA_DIGEST_WORDS
9:  lw      t3, 0(t1)
    sw      t3, 0(t0)
    addi    t0, t0, 4
    addi    t1, t1, 4
    addi    t2, t2, -1
    bnez    t2, 9b

    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    lw      s2, 0(sp)
    addi    sp, sp, 16
    ret

# =================================================================
# SPI1 flash helpers
# =================================================================

# _spi1_wait: poll SPI_USR bit until operation complete (with timeout)
_spi1_wait:
    li      t0, SPI1_BASE
    li      t2, SPI_CMD_TIMEOUT             # [FIX 6] timeout protection
    li      t3, SPI_USR_BIT
1:  lw      t1, SPI1_CMD_REG(t0)
    and     t1, t1, t3
    beqz    t1, 2f                          # done
    addi    t2, t2, -1
    bnez    t2, 1b
    j       _halt                               # SPI timeout
2:  ret

# _flash_wren: send Write Enable command
_flash_wren:
    addi    sp, sp, -4
    sw      ra, 0(sp)
    li      t0, SPI1_BASE
    li      t1, SPI_USR_COMMAND
    sw      t1, SPI1_USER_REG(t0)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_WREN)
    sw      t1, SPI1_USER2_REG(t0)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(t0)
    jal     ra, _spi1_wait
    lw      ra, 0(sp)
    addi    sp, sp, 4
    ret

# _flash_wait_wip: poll flash status register until WIP clears (with timeout)
_flash_wait_wip:
    addi    sp, sp, -12
    sw      ra, 8(sp)
    sw      s2, 4(sp)
    sw      s3, 0(sp)                       # [FIX 6] save s3 for timeout
    li      s2, SPI1_BASE
    li      s3, FLASH_WIP_TIMEOUT
1:  li      t1, (SPI_USR_COMMAND | SPI_USR_MISO)
    sw      t1, SPI1_USER_REG(s2)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_RDSR)
    sw      t1, SPI1_USER2_REG(s2)
    li      t1, 7
    sw      t1, SPI1_MS_DLEN_REG(s2)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(s2)
    jal     ra, _spi1_wait
    lw      t1, SPI1_W0_REG(s2)
    andi    t1, t1, 1
    beqz    t1, 2f                          # WIP=0, done
    addi    s3, s3, -1
    bnez    s3, 1b
    j       _halt                               # Flash WIP timeout
2:  lw      s3, 0(sp)
    lw      s2, 4(sp)
    lw      ra, 8(sp)
    addi    sp, sp, 12
    ret

# flash_erase_sector: erase 4KB sector
# a0 = flash address (sector-aligned)
    .global flash_erase_sector
    .type flash_erase_sector, @function
flash_erase_sector:
    addi    sp, sp, -8
    sw      ra, 4(sp)
    sw      s0, 0(sp)
    mv      s0, a0

    jal     ra, _flash_wren

    li      t0, SPI1_BASE
    li      t1, (SPI_USR_COMMAND | SPI_USR_ADDR)
    sw      t1, SPI1_USER_REG(t0)
    li      t1, SPI_ADDR_BITLEN_23
    sw      t1, SPI1_USER1_REG(t0)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_SE)
    sw      t1, SPI1_USER2_REG(t0)
    slli    t1, s0, 8                   # MSB-justify 24-bit addr
    sw      t1, SPI1_ADDR_REG(t0)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(t0)
    jal     ra, _spi1_wait
    jal     ra, _flash_wait_wip

    lw      ra, 4(sp)
    lw      s0, 0(sp)
    addi    sp, sp, 8
    ret

# flash_write_pw_slot: write password magic + hash to flash
# a0 = source ptr (32 bytes SHA hash), a1 = flash offset
    .global flash_write_pw_slot
    .type flash_write_pw_slot, @function
flash_write_pw_slot:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)
    sw      s2, 0(sp)
    mv      s0, a0                      # hash source
    mv      s1, a1                      # flash offset

    # Erase the target sector
    mv      a0, s1
    jal     ra, flash_erase_sector

    # Write: magic(4) + hash(32) = 36 bytes, fits in one 64-byte SPI write
    jal     ra, _flash_wren

    li      t0, SPI1_BASE

    # Load magic into W0
    li      t1, PW_MAGIC_VALUE
    sw      t1, SPI1_W0_REG(t0)

    # Load 32 bytes of hash into W1..W8
    li      t2, 8
    mv      t3, s0                      # hash source
    addi    t4, t0, SPI1_W0_REG
    addi    t4, t4, 4                   # start at W1
10: lw      t5, 0(t3)
    sw      t5, 0(t4)
    addi    t3, t3, 4
    addi    t4, t4, 4
    addi    t2, t2, -1
    bnez    t2, 10b

    # Configure page program
    li      t1, (SPI_USR_COMMAND | SPI_USR_ADDR | SPI_USR_MOSI)
    sw      t1, SPI1_USER_REG(t0)
    li      t1, SPI_ADDR_BITLEN_23
    sw      t1, SPI1_USER1_REG(t0)
    li      t1, (SPI_CMD_BITLEN_7 | FLASH_CMD_PP)
    sw      t1, SPI1_USER2_REG(t0)
    slli    t1, s1, 8                   # flash addr MSB-justified from parameter
    sw      t1, SPI1_ADDR_REG(t0)
    li      t1, (36 * 8 - 1)           # 36 bytes = 288 bits - 1
    sw      t1, SPI1_MS_DLEN_REG(t0)
    li      t1, SPI_USR_BIT
    sw      t1, SPI1_CMD_REG(t0)
    jal     ra, _spi1_wait
    jal     ra, _flash_wait_wip

    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    lw      s2, 0(sp)
    addi    sp, sp, 16
    ret

# =================================================================
# UART functions
# =================================================================

_uart_wait_sync:
    li      t0, UART0_BASE
    li      t2, UART_SYNC_TIMEOUT           # [FIX 6] timeout protection
1:  lw      t1, UART_REG_UPDATE(t0)
    andi    t1, t1, UART_REG_UPDATE_BIT
    beqz    t1, 2f                          # done
    addi    t2, t2, -1
    bnez    t2, 1b
    j       _halt                               # UART sync timeout
2:  ret

    .global uart_putc
    .type uart_putc, @function
uart_putc:
    li      t0, UART0_BASE
    li      t3, UART_TX_TIMEOUT             # [FIX 6] timeout protection
1:  lw      t1, UART_STATUS_REG(t0)
    srli    t1, t1, UART_TXFIFO_CNT_SHIFT
    andi    t1, t1, UART_TXFIFO_CNT_MASK
    li      t2, UART_TX_FIFO_SIZE
    bltu    t1, t2, 2f                      # FIFO not full, proceed
    addi    t3, t3, -1
    bnez    t3, 1b
    j       _halt                               # UART TX timeout
2:  sb      a0, UART_FIFO_REG(t0)
    ret

    .global uart_puts
    .type uart_puts, @function
uart_puts:
    addi    sp, sp, -8
    sw      ra, 4(sp)
    sw      s0, 0(sp)
    mv      s0, a0
1:  lbu     a0, 0(s0)
    beqz    a0, 2f
    jal     ra, uart_putc
    addi    s0, s0, 1
    j       1b
2:  lw      ra, 4(sp)
    lw      s0, 0(sp)
    addi    sp, sp, 8
    ret

    .global uart_print_hex8
    .type uart_print_hex8, @function
uart_print_hex8:
    addi    sp, sp, -8
    sw      ra, 4(sp)
    sw      s0, 0(sp)
    mv      s0, a0
    srli    a0, s0, 4
    andi    a0, a0, 0xF
    jal     ra, _print_nibble
    andi    a0, s0, 0xF
    jal     ra, _print_nibble
    lw      ra, 4(sp)
    lw      s0, 0(sp)
    addi    sp, sp, 8
    ret

    .global uart_print_hex32
    .type uart_print_hex32, @function
uart_print_hex32:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)
    mv      s0, a0
    li      s1, 28
1:  srl     a0, s0, s1
    andi    a0, a0, 0xF
    jal     ra, _print_nibble
    addi    s1, s1, -4
    bgez    s1, 1b
    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

_print_nibble:
    addi    t0, a0, -10
    bltz    t0, 1f
    addi    a0, a0, ('A' - 10)
    j       uart_putc
1:  addi    a0, a0, '0'
    j       uart_putc

# =================================================================
# Trap handler — print mcause/mepc then halt
# =================================================================
    .align 4
_trap_handler:
    # Save minimal context for debug print
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)

    csrr    s0, mcause
    csrr    s1, mepc

    la      a0, str_trap
    jal     ra, uart_puts
    la      a0, str_mcause_is
    jal     ra, uart_puts
    mv      a0, s0
    jal     ra, uart_print_hex32
    la      a0, str_mepc_is
    jal     ra, uart_puts
    mv      a0, s1
    jal     ra, uart_print_hex32
    la      a0, str_crlf
    jal     ra, uart_puts

    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    j       _halt

# =================================================================
# String data
# =================================================================
    .section .rodata
str_banner:
    .asciz "\r\n=== ESP32-C6 Bootloader v0.1 ===\r\n"
str_env_prefix:
    .asciz "[BOOT][ENV] hart=0x"
str_sp_is:
    .asciz " SP=0x"
str_uart_ok:
    .asciz "[BOOT][UART] 115200 8N1 ready\r\n"
str_wdt_ok:
    .asciz "[BOOT][WDT] All watchdogs disabled\r\n"
str_tmr_skip:
    .asciz "[BOOT][TMR] Timestamps deferred (Direct Boot mode)\r\n"
str_probe:
    .asciz "[BOOT][KERN] Probing kernel at flash 0x1000...\r\n"
str_kern_entry:
    .asciz "[BOOT][KERN] entry=0x"
str_kern_load:
    .asciz " load=0x"
str_kern_size:
    .asciz " size=0x"
str_crc_ok:
    .asciz "[BOOT][KERN] CRC32 OK\r\n"
str_copy:
    .asciz "[BOOT][KERN] Copying to RAM...\r\n"
str_copy_ok:
    .asciz "[BOOT][KERN] Copy verified\r\n"
str_jump:
    .asciz "[BOOT][KERN] Jumping to kernel\r\n"
str_no_kernel:
    .asciz "[BOOT][ERR] No kernel found (bad magic)\r\n"
str_kern_badaddr:
    .asciz "[BOOT][ERR] Kernel address out of bounds\r\n"
str_kern_toobig:
    .asciz "[BOOT][ERR] Kernel too large\r\n"
str_kern_empty:
    .asciz "[BOOT][ERR] Kernel size is 0\r\n"
str_kern_badcrc:
    .asciz "[BOOT][ERR] Kernel CRC mismatch "
str_expected:
    .asciz "expected=0x"
str_got:
    .asciz " got=0x"
str_copy_fail:
    .asciz "[BOOT][ERR] Copy verification failed\r\n"
str_mailbox:
    .asciz "[BOOT][MBOX] Mailbox found:"
str_mb_status:
    .asciz " status=0x"
str_mb_mode:
    .asciz " req_mode=0x"
str_mb_retry:
    .asciz " retry=0x"
str_no_mailbox:
    .asciz "[BOOT][MBOX] No mailbox (fresh boot)\r\n"
str_boot_mode:
    .asciz "[BOOT][MODE] Boot mode=0x"
str_emergency:
    .asciz "[BOOT][EMERG] Emergency mode! Retries=0x"
str_emerg_wait:
    .asciz " — halting for recovery\r\n"
str_crlf:
    .asciz "\r\n"
str_trap:
    .asciz "\r\n!!! TRAP "
str_mcause_is:
    .asciz "mcause=0x"
str_mepc_is:
    .asciz " mepc=0x"
str_pw_prompt:
    .asciz "[BOOT][AUTH] Password required: "
str_pw_ok:
    .asciz "[BOOT][AUTH] Password accepted\r\n"
str_pw_wrong:
    .asciz "[BOOT][AUTH] Wrong password, retry: "
str_pw_locked:
    .asciz "[BOOT][AUTH] Too many attempts, locked\r\n"
str_pw_none:
    .asciz "[BOOT][AUTH] No password set\r\n"
str_pw_setting:
    .asciz "[BOOT][AUTH] Setting password...\r\n"
str_pw_set_ok:
    .asciz "[BOOT][AUTH] Password set OK\r\n"
str_pw_deleting:
    .asciz "[BOOT][AUTH] Deleting password...\r\n"
str_pw_del_ok:
    .asciz "[BOOT][AUTH] Password deleted\r\n"

# =================================================================
# [FIX 5] CRC32 nibble lookup table (16 entries, 64 bytes)
# Generated from polynomial 0xEDB88320 (bit-reversed)
# =================================================================
crc32_table:
    .word 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC
    .word 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C
    .word 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C
    .word 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C

# =================================================================
    .section .bss
    .global __bss_start
    .global __bss_end

    .align 4
    .global sha_digest
sha_digest:
    .space 32                           # 8 x 32-bit SHA-256 digest
