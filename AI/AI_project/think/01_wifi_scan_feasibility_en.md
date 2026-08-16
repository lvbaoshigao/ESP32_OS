# Wi-Fi Scan Feasibility — ESP32-C6 Bare-Metal

> File status: In analysis — this note records the research and feasibility assessment for the "Wi-Fi scan" feature (user request: *"能不能扫描wifi？试试"* / "Can we scan Wi-Fi? Try it").
> Basis: `AI/doc.MD` (highest priority), `AI/how_to_read.MD`, `kernel/include/rom_wifi.h` (all ROM addresses cross-verified against ESP-IDF v6.0.2 LD scripts).
> Language: English (this is the English version of `01_wifi_scan_feasibility.md`).

---

## 1. Executive Summary

| Approach | Feasibility | Verdict |
|---|---|---|
| A. Full-decode ROM net80211 scan (returns SSID/BSSID/RSSI) | **Not feasible now** (needs ESP-IDF struct layouts + callback signatures, violates doc.MD constraints) | **Blocked** |
| B. Channel energy scan (CCA carrier sense + per-channel energy probe) | **Feasible** (all required ROM functions already listed in `rom_wifi.h`, no unknown signatures) | **Recommended** |
| C. Active beacon probe scan (probe_req → collect probe_resp) | Partly feasible, depends on RX path + frame callback (not wired yet) | **Explore** |

---

## 2. Hardware Facts (verified)

- `WDEV_BASE 0x600D0000` is **RESERVED** on ESP32-C6 [TRM 5.3-2] — the 802.11 MAC/BB register file is **NOT in the public peripheral map**. All Wi-Fi/BT control must go through on-chip ROM functions.
- ROM data pointers (per `kernel/include/rom_wifi.h` comments): `g_scan @ 0x4087ffa8`, `net80211_funcs @ 0x4087ffac`, `pp_wdev_funcs @ 0x4087ff70`, `g_chm @ 0x4087ffa4`, `g_ic_ptr @ 0x4087ffa0`, `if_ctrl_ptr @ 0x4087ff50`, `phy_param_rom @ 0x4087fce8`.
- All those pointers fall inside the HP SRAM window (`0x40800000–0x4087FFFF`), so they are **directly readable** (no load-access fault) — usable for runtime ROM introspection.
- Kernel cycle counter available: `get_mcycle()` (CSR 0x7E2, `kernel/asm/entry.S:65`) for CCA dwell timing (1 cycle @ 40MHz XTAL = 25ns).

---

## 3. Approach A: Full-decode ROM net80211 scan — currently blocked

### 3.1 Desired behavior
Call the ROM `ic_start_scan` / `g_scan` state machine to demodulate on-air beacons/probe_resps into `struct wifi_ap_t{ssid, bssid, channel, rssi, encryption}`, feeding the existing `wifisearch` table renderer (`kernel/src/software.cpp:206 soft_wifisearch`).

### 3.2 Blocking reasons
| # | Blocking point | Detail |
|---|----------------|--------|
| B1 | Layout of `struct ieee80211com` (pointed to by `g_ic_ptr`) is unknown | It is an internal ESP-IDF structure, written by the ROM into the top of SRAM at compile time. Without the layout we cannot locate the `ic_scan_start` / `ic_scan_done` offsets, so it cannot be called safely. |
| B2 | `net80211_funcs` table entry offsets unknown | Table base is known (0x4087ffac), but the function mapping per entry (`ic_mac_init` / `ic_start_scan` / …) is undocumented — would require reverse-engineering the ROM data. |
| B3 | Preconditions (wdev_mac_wakeup run, MAC initialized) unproven | `ROM_WIFI_IS_STARTED` can tell "started or not", but which hooks (RX / scan_done) get registered after start is unclear. |
| B4 | doc.MD forbids ESP-IDF components/APIs | Consulting IDF source to guess struct offsets would violate both "no copying public code" and "no conjecturing register/behavior". |

### 3.3 If unblocking later (still needs user approval)
1. Runtime-dump `g_scan` / `g_ic_ptr` structs from the ROM, pure register-level reverse engineering (no IDF source);
2. Wait until the wdev MAC path (`wdev_mac_wakeup` + `wdev_mac_reg_load`) is proven, so RX frame callbacks can receive beacons;
3. This approach gets **no coding effort now**, to avoid wasting time on an unknown ABI.

---

## 4. Approach B: Channel energy scan (CCA carrier sense) — recommended

### 4.1 Principle
When an AP is present on a channel, the channel reads as "busy" to CCA (Clear Channel Assessment). The kernel can dwell on **each channel** for a fixed time and use CCA energy detection to decide "is there a signal on this channel". This answers "**are there Wi-Fi APs around, and on which channels**" (coarse but real), and requires **no frame demodulation at all**.

### 4.2 Required ROM functions (all in `rom_wifi.h`, no unknown signatures)
| ROM function | Address | Use |
|---|---|---|
| `wifi_rf_phy_enable` | `0x40000BA8` | enable RF/PHY (prerequisite for PHY init) |
| `phy_param_rom` | `0x4087fce8` | PHY parameter table |
| `set_chan_reg` | `0x4000113C` | set channel (reg) |
| `set_channel_rfpll` | `0x4000123C` | set channel (RFPLL, includes freq setup) |
| `set_cca` | `0x40001210` | CCA threshold config |
| `set_rx_sense` | `0x40001214` | RX sense (energy detect threshold) |
| `phy_enable_cca` / `phy_disable_cca` | `0x40001344` / `0x40001340` | enable/disable CCA |
| `read_hw_noisefloor` | `0x400013A0` | read HW noise floor (per-channel energy reference) |
| `wdev_get_noise_floor` | `0x40000DDC` | get noise floor (wdev wrapper) |
| `enable_agc` / `disable_agc` | `0x4000133C` / `0x40001338` | AGC (must be enabled before energy detection) |
| `wifi_rf_phy_disable` | `0x40000BA4` | shutdown at the end |

### 4.3 Candidate signal-strength read paths
| Path | Method | Risk |
|---|---|---|
| B-P1 read noise floor directly | per-channel `read_hw_noisefloor` / `wdev_get_noise_floor`, compare against no-signal channels (1/14 edges) | High — noise floor is mostly set by hardware offset and does **not** imply an AP is on the channel |
| B-P2 CCA busy counting | dwell N ms per channel, count CCA idle→busy ratio; channels with an AP show a significantly higher busy ratio | Medium — needs a readable CCA status bit (a register), which must be found in the TRM and verified |
| B-P3 error/demod-failure rate | not applicable to passive energy scanning | — |

> Conclusion: **B-P2 (CCA busy counting)** is the primary path; if the CCA status register cannot be read, fall back to **B-P1** as a "is there a signal" reference. Since doc.MD forbids guessing register addresses, B-P2 requires locating the CCA status bit in the TRM first; **if it cannot be found, only do B-P1** (treat noise floor as a coarse energy indicator, clearly labeled as imprecise).

### 4.4 Interface with existing code
- New function: `wifi_chan_scan(struct wifi_ap_t* results, int max_results)` in `kernel/src/wifi.cpp`, declared in `wifi.h`.
- Reuse the existing `wifisearch` renderer (`soft_wifisearch` in `software.cpp` already prints the SSID/BSSID/CH/RSSI/ENC table). The energy scan fills only `channel` + `rssi` (energy); `ssid`/`bssid` stay empty and render as `<energy>` / 0.
- Precondition: `g_wifi_powered` (modem domain up, done by `modem_power_up` inside `wifi_drv_init`); the scan auto-calls `wifi_phy_calibrate()` at start (the existing `phy_init_sequence()`, 27-step ROM calibration).
- Spin-wait timing helper: `get_mcycle()` difference converted to µs/ms (`CYCLES_PER_US=40` @ 40MHz XTAL, `kernel.h:177`).

### 4.5 Milestones
| Step | Work | Exit check |
|---|---|---|
| B-M1 | make `wifi_phy_calibrate()` callable from shell (new command or reuse `wifi -cal`) | PHY calibration log appears, no trap |
| B-M2 | per-channel switch via `set_channel_rfpll` + dwell, read noise floor/energy | outputs a 1–14 channel energy table |
| B-M3 | wire into `soft_wifisearch` rendering | `wifisearch` shows per-channel energy |
| B-M4 | find CCA status bit in TRM → busy counting | channels with an AP show clearly higher busy ratio |

### 4.6 Risks
- PHY calibration may be incomplete without the ESP-IDF init → energy readings may be low or constant (accepted by design, as a "coarse probe").
- Each channel dwell needs ms-scale delay; the shell task will block (acceptable under cooperative scheduling — `ping` already does this).
- What is read is **energy**, not RSSI: converting to real RSSI needs calibration; v1 makes no promise.

---

## 5. Approach C: Active probe-req scan

- Principle: send `probe_req` (all-0xFF broadcast + wildcard SSID), APs answer `probe_resp`; demodulate `probe_resp` for SSID/BSSID/RSSI.
- Blocking points: **both TX and RX must go through the wdev MAC path** (`wdev_mac_wakeup`/`wdev_mac_reg_load` + the PP-layer RX frame callback), and the RX path is currently not wired (`wifi_drv_read` returns -1). This depends on Approach A's net80211 stack, so it is **lower priority than Approach B**.
- Current conclusion: C is the end state (yields SSID/BSSID/encryption), but depends on the TX/RX path that B does not cover. **Filed as an explore item**, to be revisited once there is evidence for the MAC path (`ROM_WIFI_IS_STARTED`, `wdev_mac_wakeup` return values).

---

## 6. Existing honest state model (do not break)

`kernel/src/wifi.cpp` maintains its state machine using mac80211 concepts:
| State | Meaning | mac80211 equivalent |
|---|---|---|
| `g_wifi_powered` | modem domain powered + APB clocked (hardware truth) | `op .start` |
| `g_wifi_ap_started` | AP configured (SSID/CH set) | `op .start_ap called` |
| `g_wifi_ap_on` | AP actually beaconing on air | `op .start_ap done` |

- `wifi_is_active()` returns true only when `g_wifi_ap_on` is true (honest semantics: an AP that transmits no beacon is not "active").
- `wifi_scan()` currently returns 0 + "not supported yet (ROM scan path not wired)".
- **The energy scan must not break this model**: it is a read-only probe and must not touch `g_wifi_ap_started` / `g_wifi_ap_on`.

---

## 6b. EMPIRICAL RESULT (2026-08-03, tested on real ESP32-C6)

Approach B was implemented and tested on hardware via `wifi -cal` / `wifi -scan`
(verbose per-ROM-step logging). Result: **Approach B is NOT viable.**

| ROM call | Address | Result on bare-metal HW |
|---|---|---|
| `wdev_get_noise_floor` | `0x40000DDC` | **Reset-safe**, returns constant `0` (PHY uncalibrated) |
| `wifi_rf_phy_enable` (PHY step 1) | `0x40000BA8` | **Resets the chip** immediately |
| `set_channel_rfpll(ch)` | `0x4000123C` | **Resets the chip** immediately |

The reset is not a clean RISC-V trap (no `KERNEL PANIC mcause=…` from
`_kern_trap`); the CPU resets and re-enters `kernel_main` with an invalid
`boot_params` ("boot_params invalid"), i.e. a watchdog/system-reset path
triggered inside the ROM function.

**Root cause:** these ROM RF/PHY leaf functions are subroutines normally
invoked by the ROM master initializer `register_chipv7_phy(phy_init_data,
cal_data, cal_mode)`, which ESP-IDF feeds a ~128-byte `phy_init_data` blob and
per-chip calibration data. Without that registration the leaf functions touch
un-initialized PHY state and the RF subsystem faults → reset. Supplying
`phy_init_data` / `register_chipv7_phy` is exactly what `doc.MD` forbids (no
ESP-IDF components/blobs, no guessing).

**Conclusion:** no scan path (energy, SSID, or beacon TX) is reachable under the
`doc.MD` constraints. The driver now reports this honestly and the crashing
sequence is gated behind an explicit `wifi -cal force` diagnostic. Approaches A
and C remain blocked for the reasons in §3/§5; Approach B is now also closed.

---

## 6c. Approach D: link the Espressif blob libs (esp32-wifi-lib) — analysed 2026-08-04

User supplied `AI/help/esp32-wifi-lib-master.zip` (Espressif prebuilt Wi-Fi
archives, incl. an **esp32c6** target). These DO contain the full scan stack:
`esp_wifi_scan_start`, `esp_wifi_scan_get_ap_records`, `scan_start`, `ppTask`,
net80211 + pp. So the capability exists — the question is whether it can be
linked and run on THIS kernel.

Symbol/size analysis (`nm`/`size` on `libcore.a` + `libnet80211.a` + `libpp.a`):

- 204 truly-external symbols. Breakdown: libgcc soft-float/int (~30, have it),
  libc (~40, newlib available), ROM data ptrs + funcs (`g_scan`, `g_ic_ptr`,
  `net80211_funcs`, `pp_wdev_funcs`, `wDevCtrl_ptr`, `ets_delay_us`,
  `read_hw_noisefloor`, `chip_v7_set_chan_misc`, `ant_*` …), the OS-adapter
  pointer `g_osi_funcs_p`, and PHY symbols.
- **`.text` size: net80211 ≈ 304 KB, pp ≈ 184 KB, core ≈ 0.4 KB → ≈ 490 KB.**

Blockers (in order of severity):

1. **Won't fit in SRAM.** ≈490 KB of Wi-Fi `.text` + 64 KB kernel + BSS + heap
   + Wi-Fi runtime buffers (~50 KB) exceeds the 512 KB HP SRAM. ESP-IDF runs
   this from **flash via the instruction-cache MMU (XIP)**; our **Direct Boot**
   copies everything to SRAM and never sets up the flash cache. Hosting the blob
   requires implementing flash XIP/cache MMU in the bootloader — a major change.
2. **libphy.a is missing.** PHY enable/calibration (`register_chipv7_phy`,
   `register_chipv7_phy_init_param`, `est_PHY_*`, `phy_change_channel`,
   `read_hw_noisefloor`, `set_rx_sense`) lives in `libphy.a` (esp_phy), which is
   NOT in this zip. Without it the radio can't be brought up. (Consistent with
   §6b: the bare ROM PHY leaves reset the chip precisely because libphy's
   `register_chipv7_phy(init_data,…)` setup is absent.)
3. **No ROM linker scripts.** ~50 ROM data-pointer/function symbols resolve via
   ESP-IDF's `esp32c6.rom*.ld` address maps, not present here.
4. **Kernel has no interrupts / no preemption.** `entry.S` sets `mtvec` to a
   panic-only trap, MIE off, scheduler is cooperative. The Wi-Fi stack is
   interrupt- + task-driven (`ppTask` blocks on an ISR-fed queue; RX beacons
   arrive by MAC interrupt). Scan cannot work without: an interrupt controller
   (INTPRI/PLIC) + Wi-Fi MAC IRQ routing, a **preemptive** scheduler, `esp_timer`
   (µs callbacks), and real semaphore/queue/mutex primitives.
5. **OS adapter + misc shims.** ~60 `wifi_osi_funcs_t` entries mapped onto the
   above, plus NVS (or stub), `esp_event`, coexist stub.

**Verdict:** Approach D is the only path that yields a *real* SSID/BSSID/RSSI
scan, but it is a large multi-phase port, and it is **currently blocked** on two
missing binary artifacts (`libphy.a` for esp32c6 + the esp32c6 ROM `.ld`
scripts, both from an ESP-IDF install) AND on kernel infrastructure that does
not exist yet (flash XIP/cache, interrupts, preemptive scheduler, esp_timer).
It cannot be started with the materials currently in `AI/help/`.

**To unblock (what the user would need to provide):**
- `libphy.a` for esp32c6 (ESP-IDF `components/esp_phy/lib/esp32c6/`) + `phy_init_data`.
- esp32c6 ROM linker scripts (`esp32c6.rom.ld`, `.rom.phy.ld`, `.rom.wifi.ld`,
  `.rom.api.ld`, `.rom.newlib*.ld`) for the ROM symbol addresses.
- Agreement to build the prerequisite kernel infra (flash cache MMU, interrupt
  controller, preemptive scheduler, esp_timer, RTOS primitives) as its own phase.

---

## 7. Next actions

1. **(Recommended) implement B-M1 of Approach B**: expose `wifi_chan_energy_scan()` in `wifi.cpp`; first verify "switch channel + read noise floor/CCA" does not trap on hardware and shows variation.
2. Add a debug command to the shell (e.g. `wifichan` or `wifi -chan`) to print the 1–14 channel energy table.
3. If no channel shows any energy difference → record "energy scan cannot distinguish signals", fall back to the ROM reverse-engineering path of Approach A/C, and update this note.
4. Check the TRM before any register/ROM-address change; this note is updated per doc.MD rules after user confirmation.

---

## Appendix: Files reviewed during this analysis

| File | Relevance |
|---|---|
| `AI/doc.MD` | highest-priority constraints: no IDF, no guessed registers, no copied public code |
| `AI/how_to_read.MD` | pitfall #11 (commented-out ROM functions may still exist), #14 (RTS = reset) |
| `kernel/include/rom_wifi.h` | complete ROM function address list + ROM data pointer comments |
| `kernel/src/wifi.cpp` | existing PHY sequence, state machine, `wifi_scan()` stub |
| `kernel/src/software.cpp` | `soft_wifisearch` renderer (scan result table) |
| `kernel/src/console.cpp` | `cmd_wifi` / `wifisearch` command dispatch |
| `kernel/src/main.cpp` | boot order (`drivers_init` → shell/gui tasks) |
| `kernel/src/bt.cpp` | BT driver (same-kind ROM driver, reference state model) |
| `kernel/asm/entry.S` | `get_mcycle()` (CSR 0x7E2) timing source |
| `kernel/Makefile` | build system (`riscv64-unknown-elf-g++`) |
