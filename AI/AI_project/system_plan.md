# ESP32-C6 System Plan (Phase 3: Kernel Subsystems + User Tool)

**Sources**: `doc.MD` (master, highest priority) + `AI/prompt/kernel/prompt_en.MD` + `AI/prompt/user/prompt_en.MD`. `prompt/boot/` excluded per instruction — already covered by `boot_plan.md`.
**Builds on**: `kernel_plan.md` (bring-up K0–K8). This plan covers the new subsystem-extension prompt and the user-tool prompt.
**Hard constraints carried over**: no ESP-IDF, direct register access, every register TRM-verified, original code in the kernel core.

---

## Conflict resolutions (doc.MD prevails)

| # | Conflict | Resolution |
|---|----------|------------|
| 1 | Both new prompts say **ESP32-C3, 400KB SRAM**; doc.MD says **ESP32-C6** | Target is C6 (RV32IMAC, 512KB HP SRAM @ 0x40800000, 16KB LP SRAM). All C3 details (8 PMP regions, 400KB memory-pool sizing, `liblibrf.a`) re-derived from the C6 TRM. |
| 2 | Kernel prompt claims "existing achievements: scheduler, memory management, IPC" | Not true — measured state below. Those are prerequisites, built first (they are kernel_plan K3–K6, folded into phase S1/S2 here). |
| 3 | Kernel prompt orders porting **FatFS R0.15 + LwIP 2.1.3**; doc.MD prohibits third-party libs "**except during driver development phase**" and requires original code | Kernel core stays original. FatFS and LwIP enter only as **driver-phase** components under `drivers/` (diskio + netif are driver code). The VFS layer and syscall surface above them are original. |
| 4 | Kernel prompt: flash with `esptool.py` to `0x0` | Existing `boot/tools/flash.py` + `pack_kernel.py` flow already works and stays — kernel image goes to `0x1000` per the established boot contract, not `0x0`. |
| 5 | Kernel prompt structure (`src/core/*.c`, `src/hal/*.hpp`, …) vs existing `kernel/` tree | Map onto the existing tree non-destructively (user prompt forbids destructive changes). See layout below; no rewrite of working files. |
| 6 | User prompt gate: "if kernel/bootloader not fully developed, do not follow this document" | Honored. The user tool is **planned here but blocked**: phase S8 cannot start until you manually confirm the kernel phases Complete (doc.MD status rule). |
| 7 | Mailbox address: kernel_plan said LP SRAM `0x50000040`; implemented code uses HP `0x4087F800` | The implemented contract wins (`kernel.h` / `boot_api.inc` agree). LP SRAM move stays a documented option, not work. |
| 8 | Kernel prompt priority order puts Network in P0 | Network moved last (S7): it is the highest-risk item (C6 Wi-Fi without IDF depends on ROM RF entry points that must be proven to exist) and nothing else depends on it. FS lands first so `ls`/`mkdir`/file commands of the user tool are unblocked either way. |

---

## Current measured state (2026-07-21)

- **Boot**: `start.S` (1112 lines) has all phases 1–8 coded: env setup, UART0, staged logs, kernel load+verify from `0x1000`, boot_params fill, mode management, emergency mode, jump. `bootloader.bin` fits the 4KB window. Status per doc.MD table: awaiting your Complete confirmation.
- **Kernel**: exists (contradicts kernel_plan's "does not exist"). `entry.S` (110), `main.cpp` (72), `console.cpp` (269, mini-shell: help/info/uptime/mode/drivers/reboot/log/version), `espsys.cpp` (149, driver registry: serial ok, network/video stubs), `kernel.h` (146, boot_params + mailbox contracts). Mailbox OK-write on boot works.
- **Missing** (= this plan): traps/interrupts, timer tick, memory manager, scheduler, IPC, HAL classes, flash driver, partition table, VFS/FatFS, users/permissions, PMP/safe-mode, network, full user shell.
- **Tools on hand**: `boot/tools/flash.py`, `mkkernel.py`, `mkpasswd.py` (reuse for S5 passwords), `kernel/tools/pack_kernel.py`.

---

## Target layout (non-destructive mapping of the prompt's structure)

```
kernel/
├── asm/        entry.S (exists) + trap.S, ctxsw.S
├── src/        main.cpp, console.cpp, espsys.cpp (exist)
│               + sched.cpp, mm.cpp, ipc.cpp          # core (original C, prompt's src/core/)
│               + vfs.cpp, syscall.cpp, pmp.cpp, users.cpp
├── hal/        uart.hpp, gpio.hpp, spi.hpp, timer.hpp # C++ register wrappers (prompt's src/hal/)
├── include/    kernel.h (exists) + klog.h, vfs.h, user_api.h
├── linker.ld, Makefile (exist — extend, don't replace)
└── tools/      pack_kernel.py (exists) + test scripts
drivers/        flash/spi_flash.cpp, fatfs/ (3rd-party, R0.15), diskio/,
                lwip/ (3rd-party, 2.1.3), wifi/        # only place 3rd-party code may live
```

Logging: extend existing `klog`/`kprintf` with levels + `klog_set_level()`; tags `[INIT] [SCHED] [NET] [FS] [ERR]` per kernel prompt, keeping the existing `[KERN]` banner flow.

---

## Phases

Statuses per doc.MD rules: Pending → In Progress automatic when coding starts; → Complete **only by your manual confirmation**.

| # | Task | Prompt ref | Work items | Exit check | Status |
|---|------|-----------|------------|------------|--------|
| S0 | Env verification (every session) | doc.MD P0 | serial device + rw perms, `riscv` toolchain, board reachable; QEMU `qemu-system-riscv32` presence for hw-independent tests | `serial_check` exits 0 | per session |
| S1 | Traps, time, memory (kernel base) | P0 sched prereq + P0 mem | trap.S dispatch (mcause table), interrupt matrix routing (TRM INTMTX — verify before coding), timer tick (SYSTIMER alarm; re-probe valid-bit quirk, mcycle fallback), `mm_alloc`/`mm_free` free-list over fixed map, `mm_swap_init`/`mm_zram_init` **stubs** + `mm_swap_info` (LZ4 interface reserved, not implemented — as specified) | forced illegal instr → handler prints mcause/mepc; tick ≈1s vs wall clock; alloc/free stress passes; `mm_swap_info()` returns status without crash | Pending |
| S2 | Preemptive scheduler + IPC | P0 | ctxsw.S (full int context; no F ext on C6 — no FP save), time-slice round-robin + priority preemption, task create/exit, idle=`wfi`, switch counter; minimal IPC (message queue over mm_alloc) | two tasks, different priorities: high preempts low, log shows switch count | Pending |
| S3 | HAL layer (C++) | prompt §2 | `Uart`, `Gpio`, `Spi`, `Timer` classes wrapping the already-verified register blocks; migrate call sites incrementally (console first), never break the running build | kernel builds and boots identically after each migration step | Pending |
| S4 | Flash driver + partitions + VFS/FatFS | P0 VFS + P1 partition | `drivers/flash/spi_flash.cpp` (C6 SPI1 controller: read/write/erase, TRM chapter first); partition-table parse (list name/start/size); FatFS R0.15 as third-party driver-phase code (`FF_USE_LFN` on, exFAT off), diskio glue; original VFS: `open/read/write/close/lseek` | partitions listed; file created on flash, survives reboot | Pending |
| S5 | Users & permissions | P1 | user create/delete/passwd (hash via existing `mkpasswd.py` scheme), root vs ordinary, permission check in syscall layer (`syscall_table` + `user_api.h`) | ordinary user denied root-only file access | Pending |
| S6 | Security & exceptions | P1 | Safe Mode (interrupts off, minimal services), Emergency Mode (ties into existing boot mailbox: `KERN_STATUS_PANIC` path), PMP config — **C6 PMP region count/granularity from TRM, not C3's 8** — isolate kernel text/data | forced illegal memory access → PMP trap → emergency mode logged, mailbox updated | Pending |
| S7 | Network (LwIP + Wi-Fi AP) | P0 (moved last, res. #8) | verify C6 ROM RF entry points exist and are callable without IDF blobs (ROM ELF symbol list first — **go/no-go gate**); if go: LwIP 2.1.3 driver-phase port (TCP/IP only, pools sized for remaining SRAM), AP mode SSID `ESP32` pass `12345678`; if no-go: document blocker, network driver stays `n/a` (espsys already models this) | phone sees + connects to hotspot, log shows connection — or a written no-go finding | Pending |
| S8 | User tool (Bash-style shell) — **GATED** | user prompt | blocked until kernel confirmed Complete (res. #6). Launch as first task at kernel start (extend `main.cpp` task spawn — non-destructive); prompt `username/device_name:`; extend existing `console_dispatch` with: `ls mkdir com net newfile pwd rename cd disk device chdiv wifi` (+ existing `help reboot`); `-help` on every command; `chdiv` swappable delimiter (default whitespace) in one tokenizer | every command parses, `-help` works, delimiter switch via `chdiv "/"` works, `disk -free/-part/-parted`, `wifi -on/-off/-changepasswd` behave per spec | **Blocked** |
| S9 | HTTP boot-log page + polish | P2 optional | only if S7 = go: serve boot log (white on black, streaming) at `192.168.4.1` | page loads on connected phone | Pending |

---

## Testing & deliverables (kernel prompt §5–6)

- Per module: one `test_<module>` (task or host-side serial-capture assert) — smallest check that fails if the logic breaks. QEMU (`rv32 virt`) only for hardware-independent logic: VFS over a RAM disk, scheduler algorithm. Everything register-touching verifies on the real board via existing `flash.py` + miniterm flow.
- Deliverables at the end: `build.sh`, `flash.sh` (thin wrappers over existing Makefile + flash.py), `log_boot.txt` sample, per-module test notes, `docs/design.md` (module interfaces + dependencies).

## Risks

| Risk | Mitigation |
|------|-----------|
| C6 Wi-Fi infeasible without IDF blobs | S7 go/no-go gate before any LwIP work; documented no-go is an acceptable exit |
| FatFS/LwIP RAM footprint on 512KB shared with tasks | fixed memory map in S1 reserves pools up front; LwIP TCP-only, exFAT off |
| PMP granularity/count differs from C3 assumptions | TRM lookup is the first work item of S6; no coding before |
| SYSTIMER valid-bit quirk (carried from boot) | re-probe in S1 kernel context; mcycle fallback keeps tick alive |
| Stack sizing (prompt warns 1–2KB) | per-task stack + canary word checked on every context switch |
| Third-party code leaking into kernel core | 3rd-party only under `drivers/`; review checkpoint at S4/S7 merge |
