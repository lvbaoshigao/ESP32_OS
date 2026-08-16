# ESP32-C6 Kernel Entry Point
# Receives a0 = pointer to boot_params_t from bootloader
# UART0 already initialized, watchdogs disabled, M-mode, interrupts off

    .section .text.entry, "ax"
    .global _kernel_start
    .type _kernel_start, @function
_kernel_start:
    # Save boot_params pointer in s0 (callee-saved, survives BSS clear)
    mv      s0, a0

    # Set kernel stack (below boot_params region)
    li      sp, 0x4087C000

    # Install kernel trap handler
    la      t0, _kern_trap
    csrw    mtvec, t0

    # Clear kernel BSS
    la      t0, __kern_bss_start
    la      t1, __kern_bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    # Run .init_array constructors (C++ static initializers)
    la      t0, __init_array_start
    la      t1, __init_array_end
    j       4f
3:  lw      a0, 0(t0)                  # load function pointer
    jalr    ra, a0                     # call it
    addi    t0, t0, 4
4:  blt     t0, t1, 3b

    # Enable performance counter (cycle counting) once at boot
    # CSR 0x7E0 = mpcer (M-mode Performance Counter Enable Register) [TRM 3.1.2]
    # Set bit 0 to enable cycle counter
    li      t0, 1
    csrw    0x7E0, t0                   # mpcer: enable cycle count

    # NOTE: Standard mcycle CSR (0xB00) is available per RISC-V privileged spec [v1.12]
    # and may be used instead of custom CSR 0x7E2. To switch:
    #   - Define USE_STANDARD_MCYCLE in kernel.h
    #   - Replace get_mcycle below with: csrr a0, mcycle (CSR 0xB00)
    #   - Remove the csrw 0x7E0 above (standard mcycle auto-counts in M-mode)

    # Store boot_params pointer into global (BSS now zeroed, safe to write)
    la      t0, g_boot_params
    sw      s0, 0(t0)

    # Call kernel_main(boot_params)
    mv      a0, s0
    jal     ra, kernel_main

    # kernel_main should never return
_kern_halt:
    wfi
    j       _kern_halt

# read performance counter — callable from C++
# ESP32-C6 uses custom CSR mpccr (0x7E2) instead of standard mcycle (0xB00)
# [TRM 3.1.3] mpccr = M-mode Performance Counter Count Register
# ESP32-C6 TRM: custom CSR 0x7E2 reads the cycle counter in HP SRAM domain
    .global get_mcycle
    .type get_mcycle, @function
get_mcycle:
    csrr    a0, 0x7E2
    ret

# Trap handler — panic print and halt
# NOTE: This is a panic-only handler that never returns.
    .align 4
    .global _kern_trap
_kern_trap:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)

    csrr    s0, mcause
    csrr    s1, mepc

    la      a0, _str_trap
    jal     ra, uart_puts

    mv      a0, s0
    jal     ra, _kern_print_hex32

    la      a0, _str_mepc
    jal     ra, uart_puts

    mv      a0, s1
    jal     ra, _kern_print_hex32

    la      a0, _str_crlf
    jal     ra, uart_puts

    # Halt
    j       _kern_halt

# Minimal hex32 print for trap handler (avoids kprintf dependency)
_kern_print_hex32:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      s1, 4(sp)
    mv      s0, a0
    li      s1, 28
1:  srl     a0, s0, s1
    andi    a0, a0, 0xF
    li      t0, 10
    blt     a0, t0, 2f
    addi    a0, a0, ('A' - 10)
    j       3f
2:  addi    a0, a0, '0'
3:  jal     ra, uart_putc
    addi    s1, s1, -4
    bgez    s1, 1b
    lw      ra, 12(sp)
    lw      s0, 8(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

    .section .rodata
_str_trap:
    .asciz "\r\n!!! KERNEL PANIC mcause=0x"
_str_mepc:
    .asciz " mepc=0x"
_str_crlf:
    .asciz "\r\n"

    .section .bss
    .global g_boot_params
g_boot_params:
    .word 0
