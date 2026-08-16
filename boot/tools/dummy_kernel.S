# Dummy kernel — prints a message and halts
# Loaded to SRAM by bootloader, runs from RAM

.equ UART0_BASE,        0x60000000
.equ UART_FIFO_REG,     0x0000
.equ UART_STATUS_REG,   0x001C

    .section .text
    .global _kernel_start
_kernel_start:
    la      a1, msg
1:  lbu     a2, 0(a1)
    beqz    a2, 2f
    li      a3, UART0_BASE
3:  lw      a4, UART_STATUS_REG(a3)
    srli    a4, a4, 16
    andi    a4, a4, 0xFF
    li      a5, 128
    bgeu    a4, a5, 3b
    sb      a2, UART_FIFO_REG(a3)
    addi    a1, a1, 1
    j       1b
2:  wfi
    j       2b

msg:
    .asciz "[KERNEL] Hello from kernel! Boot successful.\r\n"
