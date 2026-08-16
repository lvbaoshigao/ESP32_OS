# ESP32-C6 Cooperative Context Switch
# RISC-V ABI: save/restore callee-saved registers (ra, s0-s11, sp)
#
# void ctx_switch(uint32_t* save_sp, uint32_t load_sp);
#   a0 = pointer to save location for current task's SP
#   a1 = new task's saved SP value to load

    .section .text
    .global ctx_switch
    .type ctx_switch, @function
ctx_switch:
    # Save callee-saved registers on current stack
    addi    sp, sp, -52
    sw      ra, 48(sp)
    sw      s0, 44(sp)
    sw      s1, 40(sp)
    sw      s2, 36(sp)
    sw      s3, 32(sp)
    sw      s4, 28(sp)
    sw      s5, 24(sp)
    sw      s6, 20(sp)
    sw      s7, 16(sp)
    sw      s8, 12(sp)
    sw      s9,  8(sp)
    sw      s10, 4(sp)
    sw      s11, 0(sp)

    # Save current SP to *save_sp (a0)
    sw      sp, 0(a0)

    # Load new SP from load_sp (a1)
    mv      sp, a1

    # Restore callee-saved registers from new stack
    lw      ra, 48(sp)
    lw      s0, 44(sp)
    lw      s1, 40(sp)
    lw      s2, 36(sp)
    lw      s3, 32(sp)
    lw      s4, 28(sp)
    lw      s5, 24(sp)
    lw      s6, 20(sp)
    lw      s7, 16(sp)
    lw      s8, 12(sp)
    lw      s9,  8(sp)
    lw      s10, 4(sp)
    lw      s11, 0(sp)

    addi    sp, sp, 52

    ret

# Preemptive context switch — saves ALL registers + mepc/mcause/mstatus
# For use from timer/IRQ trap handler (preemptive scheduling)
# ESP32-C6 has no FPU (RV32IMAC), FP registers not saved
#
# void ctx_switch_full(uint32_t* save_sp, uint32_t load_sp);
#   a0 = pointer to save location for current task's SP
#   a1 = new task's saved SP value to load
    .global ctx_switch_full
    .type ctx_switch_full, @function
ctx_switch_full:
    # Save all 32 GP registers + mepc/mcause/mstatus = 35 words (140 bytes)
    # Layout: x0..x31, mepc, mcause, mstatus
    # x0 (zero) skipped to save space, stored as 0 for uniform layout
    # x2 (sp) saved separately via save_sp pointer, stored here for uniformity
    addi    sp, sp, -140
    sw      x0,   0(sp)                # x0  (zero)
    sw      ra,   4(sp)                # x1  (ra)
    sw      sp,   8(sp)                # x2  (sp)
    sw      gp,  12(sp)                # x3  (gp)
    sw      tp,  16(sp)                # x4  (tp)
    sw      t0,  20(sp)                # x5  (t0)
    sw      t1,  24(sp)                # x6  (t1)
    sw      t2,  28(sp)                # x7  (t2)
    sw      s0,  32(sp)                # x8  (s0/fp)
    sw      s1,  36(sp)                # x9  (s1)
    sw      a0,  40(sp)                # x10 (a0)
    sw      a1,  44(sp)                # x11 (a1)
    sw      a2,  48(sp)                # x12 (a2)
    sw      a3,  52(sp)                # x13 (a3)
    sw      a4,  56(sp)                # x14 (a4)
    sw      a5,  60(sp)                # x15 (a5)
    sw      a6,  64(sp)                # x16 (a6)
    sw      a7,  68(sp)                # x17 (a7)
    sw      s2,  72(sp)                # x18 (s2)
    sw      s3,  76(sp)                # x19 (s3)
    sw      s4,  80(sp)                # x20 (s4)
    sw      s5,  84(sp)                # x21 (s5)
    sw      s6,  88(sp)                # x22 (s6)
    sw      s7,  92(sp)                # x23 (s7)
    sw      s8,  96(sp)                # x24 (s8)
    sw      s9, 100(sp)                # x25 (s9)
    sw      s10, 104(sp)               # x26 (s10)
    sw      s11, 108(sp)               # x27 (s11)
    sw      t3, 112(sp)                # x28 (t3)
    sw      t4, 116(sp)                # x29 (t4)
    sw      t5, 120(sp)                # x30 (t5)
    sw      t6, 124(sp)                # x31 (t6)
    csrr    t0, mepc
    sw      t0, 128(sp)                # mepc
    csrr    t0, mcause
    sw      t0, 132(sp)                # mcause
    csrr    t0, mstatus
    sw      t0, 136(sp)                # mstatus

    # Save current SP to *save_sp (a0)
    sw      sp, 0(a0)

    # Load new SP from load_sp (a1)
    mv      sp, a1

    # Restore all registers from new stack
    lw      x0,   0(sp)                # x0  (zero) — restores to zero reg (no-op)
    lw      ra,   4(sp)                # x1  (ra)
    # sp (x2) loaded last, skip for now
    lw      gp,  12(sp)                # x3  (gp)
    lw      tp,  16(sp)                # x4  (tp)
    lw      t0,  20(sp)                # x5  (t0)
    lw      t1,  24(sp)                # x6  (t1)
    lw      t2,  28(sp)                # x7  (t2)
    lw      s0,  32(sp)                # x8  (s0/fp)
    lw      s1,  36(sp)                # x9  (s1)
    lw      a0,  40(sp)                # x10 (a0)
    lw      a1,  44(sp)                # x11 (a1)
    lw      a2,  48(sp)                # x12 (a2)
    lw      a3,  52(sp)                # x13 (a3)
    lw      a4,  56(sp)                # x14 (a4)
    lw      a5,  60(sp)                # x15 (a5)
    lw      a6,  64(sp)                # x16 (a6)
    lw      a7,  68(sp)                # x17 (a7)
    lw      s2,  72(sp)                # x18 (s2)
    lw      s3,  76(sp)                # x19 (s3)
    lw      s4,  80(sp)                # x20 (s4)
    lw      s5,  84(sp)                # x21 (s5)
    lw      s6,  88(sp)                # x22 (s6)
    lw      s7,  92(sp)                # x23 (s7)
    lw      s8,  96(sp)                # x24 (s8)
    lw      s9, 100(sp)                # x25 (s9)
    lw      s10, 104(sp)               # x26 (s10)
    lw      s11, 108(sp)               # x27 (s11)
    lw      t3, 112(sp)                # x28 (t3)
    lw      t4, 116(sp)                # x29 (t4)
    lw      t5, 120(sp)                # x30 (t5)
    lw      t6, 124(sp)                # x31 (t6)
    lw      t0, 128(sp)
    csrw    mepc, t0
    lw      t0, 132(sp)
    csrw    mcause, t0
    lw      t0, 136(sp)
    csrw    mstatus, t0

    # Restore sp last (after all CSR writes, before the rest of restore)
    lw      sp, 8(sp)

    # Decrement saved sp by 140 to skip the frame (sp was saved before adjustment)
    # Actually sp was saved AFTER addi sp,sp,-140, so saved_sp = adjusted_sp
    # We need to advance past the save frame: saved_sp + 140
    addi    sp, sp, 140

    mret
