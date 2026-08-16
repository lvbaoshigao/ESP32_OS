# ESP32-C6 Kernel — Development Plan (Phase 2: Kernel)

**Scope**: kernel image format + boot↔kernel ABI (the "interfaces defined at this stage" per doc.MD), then kernel bring-up from entry to a minimal scheduled system with boot feedback.
**Target**: ESP32-C6 (QFN40) rev v0.1, RV32IMAC @ 160MHz, 512KB HP SRAM @ 0x40800000, 16KB LP SRAM @ 0x50000000.
**Languages**: assembly for entry/trap/context-switch, C++ (freestanding: no exceptions, no RTTI, no std heap) for everything else, Python for tools.
**Hard constraints**: no ESP-IDF, no third-party libs, no copied public code, direct register access only, every register verified against the C6 TRM before use.

---

## Current state (measured 2026-07-20)

- **Boot**: phases 1–3 coded. `bootloader.bin` = 949 bytes → fits the 4KB window; **kernel offset 0x1000 stands**. Boot runs XIP from flash mapped at `0x42000000` (Direct Boot), UART0 115200 8N1 works, all watchdogs disabled, trap handler prints mcause/mepc.
- **Boot gaps**: GP never set (doc.MD Phase 2 requires it — fix when first `.sdata` access appears); `boot_log`/timestamp path exists but main flow prints without it. Phases 4–8 pending — **they consume the contracts below**.
- **Kernel**: `kernel/` does not exist. `AI/prompt/kernel/` is empty — doc.MD + this plan govern.
- **Hardware finding carried over**: SYSTIMER valid-bit unreliable in Direct Boot mode on this board; `mcycle` works (see `start.S` comments). Re-probe in kernel context before trusting SYSTIMER.

---

## Contracts first (boot↔kernel ABI)

These four items are the load-bearing part; boot phases 4–7 and kernel K0/K8 both implement against them. One shared header (`boot_api.h`, `#ifdef __ASSEMBLER__` guards so asm and C++ read the same file), all magics/offsets defined once.

### 1. Kernel image at flash `0x1000` (memory-mapped at `0x42001000`)

32-byte header, little-endian, then payload:

| Offset | Field | Notes |
|--------|-------|-------|
| 0x00 | magic `0x4B36534F` ("OS6K") | reject anything else |
| 0x04 | header_version (=1) | |
| 0x08 | entry | absolute, must lie in [load_addr, load_addr+length) |
| 0x0C | load_addr | fixed `0x40800000` for now |
| 0x10 | length | payload bytes; max `0x7F000` (508KB — top 4KB is boot's working stack during copy) |
| 0x14 | crc32 | poly `0xEDB88320`, bitwise, over payload only |
| 0x18–0x1F | reserved | zero |

Built by `tools/pack_image.py` (also emits deliberately-corrupt fixtures for boot Phase 4 testing).

### 2. Handoff protocol

Boot: parse header → copy payload `0x42001020` → `load_addr` → CRC32 verify → `fence.i` → jump to `entry` with `a0 = &boot_params`. Kernel runs **from SRAM**, not XIP — so it can later own the SPI flash/cache without executing from what it's reprogramming. Ceiling: kernel > 508KB → switch to XIP layout (only .data/.bss in SRAM); documented, not built.

### 3. boot_params (boot→kernel, read-only for kernel)

Static block at `0x50000040` (LP SRAM), pointer passed in `a0` per RISC-V ABI:
`magic, api_version, boot_mode, boot_source, retry_count, cpu_freq_hz, xtal_freq_hz, flags (bit0 = systimer_quirk), reserved[4]`.

### 4. Mailbox (kernel→boot feedback, survives warm reset)

At `0x50000000` (LP SRAM): `magic, status, requested_mode, boot_fail_count, reserved[4]`. Boot reads/validates it before anything else on next reset; kernel writes status (BOOT_OK clears fail counter) and mode requests (safe/recovery/factory → boot phase 6 branches).

LP SRAM retention across the relevant reset types **must be TRM-verified** (LP system chapter) before relying on it; fallback = HP SRAM block + magic. First 128B of LP SRAM reserved for this ABI — if the LP core later needs it, move the block (one-line change, both sides read the shared header).

---

## File layout

```
kernel/
├── asm/        entry.S, trap.S, ctxsw.S
├── src/        main.cpp, console.cpp, timer.cpp, mem.cpp, sched.cpp, api.cpp
├── include/    kernel.h, boot_api.h          # boot includes boot_api.h via -I; reg_c6.inc reused from boot/include (no duplicate)
├── linker.ld   # link at 0x40800000
├── Makefile    # same toolchain/flags as boot: riscv64-unknown-elf-, rv32imac_zicsr, ilp32
└── tools/      pack_image.py, mk_bad_images.py
```

---

## Phases

| # | Task | Work items | Exit check |
|---|------|-----------|------------|
| K0 | Contracts + stub kernel | `boot_api.h`, `pack_image.py`, minimal asm-only stub: set SP, print "hello from kernel" + dump boot_params fields over UART | Stub image at 0x1000 boots via real bootloader on hardware. **Unblocks boot Phases 4–5** |
| K1 | Entry & runtime env | own SP (top of kernel region) / GP / vectored mtvec, .bss clear, static ctors, C++ freestanding glue; panic = print + halt | Trap on purpose → kernel's own handler prints mcause/mepc; C++ main reached |
| K2 | Console | kernel-owned UART0 driver (inherits boot's init, re-init only if needed), putc/puts/printf-lite, log macro `[KRN][<stage>][<uptime>]` | Formatted log on real UART |
| K3 | Traps & interrupts | trap dispatch table, interrupt matrix routing (TRM INTMTX chapter — verify register set before coding), enable/disable/claim API | Forced illegal instruction and one routed peripheral IRQ both land in the right handler |
| K4 | Time | tick: SYSTIMER alarm → IRQ; **first re-probe the valid-bit quirk outside Direct Boot context**, fallback = mcycle-derived tick; uptime_us, delay | Tick measured ~1s against wall clock; calibration constant kept in one place (real 40MHz crystal drifts) |
| K5 | Memory | fixed map: kernel image / heap / task stacks / guard; free-list allocator with alloc/free; PMP protection skipped (extension, note only) | Alloc/free stress check passes, no overlap with map |
| K6 | Scheduling | context switch (asm), round-robin preemptive on tick, task create/exit, idle task = `wfi` | Two demo tasks interleave; idle time visible via cycle counter |
| K7 | Kernel API | versioned function table (flat M-mode calls; `ecall`/U-mode reserved as extension field, not built): console, time, mem, task | Smoke-test task exercises every table entry |
| K8 | Boot feedback loop | write mailbox status + mode requests; joint test with boot Phases 6–7: normal boot clears fail counter, forced failure ×3 → boot emergency path | End-to-end on hardware: normal / safe / recovery transitions logged on both sides |

**Status rules** (per doc.MD): all phases Pending. Pending → In Progress automatically when coding starts; In Progress → Complete only on your manual confirmation — never self-marked.

---

## Order & coupling with boot

1. **K0 first** — it is the "interfaces only" deliverable doc.MD allows in the current project phase, and boot Phase 4 (probe/load) and Phase 5 (API) cannot be tested without it.
2. Then boot 4 → boot 5 against the K0 stub.
3. K1–K7 proceed while boot 6–7 (modes, emergency) are built against the mailbox spec.
4. K8 + boot 6/7/8 joint end-to-end, then boot Phase 8 sign-off.
5. Note: K1 onward is real kernel development — doc.MD currently says "boot phase, interfaces only", so starting K1 means the project has moved to Phase 2; that switch is yours to call.

Verification: real board is primary (existing `flash.py` + miniterm flow at `/dev/ttyACM0`); QEMU only if the Espressif fork's C6 machine proves usable (unresolved from boot plan Phase 0). Every phase leaves one runnable check (test task or serial-capture assert script). Register work order stays: TRM lookup → `.inc`/header entry with chapter citation → code.

---

## Risks

| Risk | Mitigation |
|------|-----------|
| SYSTIMER quirk is silicon rev v0.1 errata, not Direct-Boot-specific | Probe first thing in K4; check errata sheet; mcycle fallback keeps K4 unblocked |
| LP SRAM doesn't retain mailbox across the reset types boot cares about | TRM-verify retention + reset matrix before K8; fallback HP SRAM mailbox with magic validation |
| C++ freestanding gaps on rv32imac (libgcc soft ops, ctor ordering) | K1 proves the runtime with a trivial C++ main before anything depends on it |
| Kernel outgrows 508KB SRAM budget | XIP upgrade path already specified in contract §2; header format unchanged |
| Boot/kernel header drift | Single shared `boot_api.h` consumed by both builds; no second definition anywhere |
