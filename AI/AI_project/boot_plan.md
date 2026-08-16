# ESP32-C6 Bootloader — Development Plan (Phase 1: Boot)

**Scope**: hardware reset → kernel handoff only. No kernel, drivers, or GUI work.
**Target**: ESP32-C6 (QFN40) rev v0.1, RV32IMAC @ 160MHz, 40MHz crystal, 512KB HP SRAM + 16KB LP SRAM.
**Languages**: RISC-V assembly (core), C++ (auxiliary), Python (debug/test tooling only).
**Hard constraints**: no ESP-IDF, no third-party libs, no copied public code, direct register access only, every register address verified against the ESP32-C6 Technical Reference Manual before use (no guessing).

---

## Known discrepancies (resolve before coding)

1. **C3 vs C6**: `prompt/boot/prompt_en.MD` describes an ESP32-C3 bootloader; `doc.MD` (master) specifies ESP32-C6. Plan follows doc.MD → **target is C6**. The prompt_en workflow/constraints (GAS syntax, `.section .text.boot`, `.global _start`, ABI compliance, staged debug logs, QEMU-first verification) still apply; all C3 register addresses and boot details must be re-derived from the **C6** TRM, not carried over.
2. **Flash layout tension**: ROM loads the 2nd-stage bootloader from flash `0x0`; the kernel image lives at `0x1000`. That leaves ≤4KB of flash (minus the ROM-required image header) for the bootloader. Either the bootloader fits in that window (feasible in tight assembly) or the kernel offset must move — measure the binary at the end of Phase 3 and decide then. Until measured, treat **4KB total bootloader size** as a budget.
3. **ROM image format**: the C6 boot ROM expects a specific image header/segment layout at `0x0`. Exact format must be pulled from the C6 TRM / official Espressif image-format documentation before the first flash attempt. Hand-build the header (assembler directives or a small Python packer) — no esptool image generation logic copied.

---

## Deliverables & file layout

```
boot/
├── asm/        start.S (entry, env setup), uart.S, flash.S, jump.S, vectors.S
├── src/        (C++ auxiliary only if a task is genuinely awkward in asm)
├── include/    reg_c6.h/.inc (verified register addresses, one place), boot_api.h (boot↔kernel ABI)
├── linker.ld   memory layout (IRAM/DRAM placement per C6 TRM)
└── tools/      pack_image.py (image header packer), serial_check.py (Phase 0), test scripts
```

Each phase leaves one runnable check (QEMU run script or serial-capture assert script).

---

## Phases

| # | Task | Work items | Exit check |
|---|------|-----------|------------|
| 0 | Environment verification (**every session**) | Serial device present (`/dev/ttyUSB*`/`ttyACM*`) + rw permission; RISC-V GCC toolchain (`riscv32-*` or multilib) present and version logged; QEMU with C6 (or nearest) machine support; minicom/screen or Python serial available | `tools/serial_check.py` exits 0 |
| 1 | Hardware init (asm) | From C6 TRM: minimal clock config (run from default/safe clock first, 160MHz later if needed on boot path), UART0 pin + peripheral config via direct registers, SPI flash controller minimal init (DIO target) | Binary assembles; QEMU boots to first instruction |
| 2 | Execution environment | `_start` in `.section .text.boot`; set SP (top of DRAM region from TRM), GP, MTVEC/MTVT base; clear .bss; `.option arch` control for compressed instructions; RISC-V ABI throughout | Stack usable: a call/return round-trip runs |
| 3 | Serial debug output | UART0 TX polled driver in asm; log format `[BOOT][<stage>][<timestamp>]`; timestamp from a TRM-verified counter (systimer/mcycle); print hex helper; log `mhartid`, key addresses at each stage | Logs visible in QEMU serial and on real UART; measure binary size vs 4KB budget |
| 4 | Kernel probing & loading | Read kernel image from flash `0x1000` via SPI flash controller (or memory-mapped read if TRM confirms cache mapping); header parse: magic, entry, load addr, length; integrity = checksum/CRC over image | Corrupt image → detected + logged; valid image → loaded to RAM |
| 5 | API framework | `boot_api.h`: boot-parameter struct passed to kernel (a0 = pointer, per RISC-V ABI): boot mode, hardware info, API version, **reserved fields** for extension; kernel→boot feedback slot (fixed RAM mailbox address + magic, survives kernel start) | Struct documented; dummy kernel stub echoes params over UART |
| 6 | Boot mode management | Modes: normal / safe / recovery / factory; selection from mailbox feedback (retry counter, kernel-requested mode) and boot-source table (main flash, backup partition, external storage — backup/external as table entries, only main flash implemented now) | Forced mode via mailbox → correct branch taken, logged |
| 7 | Emergency mode | Timeout + error handling on every critical step (flash read, verify, jump); N consecutive failures (mailbox retry counter) → emergency: minimal UART console loop announcing state, waiting for recovery action | Simulated bad kernel 3× → emergency entered, logged |
| 8 | Control transfer | Disable/park what init enabled beyond kernel needs, fence.i, jump to verified entry with a0=boot params; self-review pass of every instruction against TRM + RISC-V spec | End-to-end in QEMU: full staged log → dummy kernel runs; then real-hardware verification (flash timing, register deltas) |

**Status rules** (per doc.MD): all phases Pending. A phase moves to In Progress automatically when its coding starts; In Progress → Complete only on your manual confirmation — I never mark Complete.

---

## Order & verification strategy

- Phases run 1→8; 5/6/7 are design-coupled (mailbox + modes + emergency share the feedback mechanism) — design their data structures together in Phase 5, implement incrementally.
- QEMU first for every phase, real ESP32-C6 hardware second (flash timing, strap pins, register differences re-checked on silicon). Keep a calibration point for the timestamp source — real 40MHz crystal vs QEMU virtual time will differ.
- Reference priority: `prompt/boot` English docs → C6 TRM (`AI/help/esp32-c6_datasheet_cn.pdf` + fetched TRM) → RISC-V spec (`AI/help/riscv-spec.*`) → Linux source for concepts only (no code).
- All register addresses live in one include file with a TRM section citation per entry, so review = one file against the manual.

---

## Risks

| Risk | Mitigation |
|------|-----------|
| 4KB flash window too small | Measure at Phase 3; if exceeded, propose moving kernel offset (needs your sign-off — layout is in doc.MD) |
| QEMU lacks/limits ESP32-C6 machine | Verify Espressif QEMU fork C6 support at Phase 0; fall back to earlier hardware bring-up with UART-log-driven debugging |
| Datasheet vs TRM gap (datasheet ≠ full register map) | Fetch official C6 TRM early (permitted via network); block coding on any unverified register |
| rev v0.1 silicon errata | Check errata sheet before real-hardware phase |
