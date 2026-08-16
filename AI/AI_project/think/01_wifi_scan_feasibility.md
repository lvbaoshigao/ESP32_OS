# Wi-Fi 扫描可行性分析 — ESP32-C6 裸机 / Wi-Fi Scan Feasibility — ESP32-C6 Bare-Metal

> 文件状态 / File status：分析中 / In analysis — 本文记录"Wi-Fi 扫描"功能的调研与可行性判断（用户请求 / user request: *"能不能扫描wifi？试试"*）。
> 依据 / Basis：`AI/doc.MD`（最高优先级 / highest priority）、`AI/how_to_read.MD`、`kernel/include/rom_wifi.h`（ROM 地址均来自 ESP-IDF v6.0.2 LD 脚本，已交叉验证）。
> 语言 / Language：中文为主，英文为辅（双语 / bilingual, Chinese primary, English supporting）。

---

## 1. 结论摘要 / Executive Summary

| 方案 / Approach | 可行性 / Feasibility | 结论 / Verdict |
|---|---|---|
| A. ROM net80211 全解调扫描（返回 SSID/BSSID/RSSI）<br>Full-decode ROM net80211 scan | 当前**不可行**（缺 ESP-IDF 结构体布局与回调签名，违反 doc.MD 禁令）<br>Not feasible now (needs ESP-IDF struct layouts + callback signatures, violates doc.MD) | **阻塞 / Blocked** |
| B. 信道能量扫描（CCA 载波侦听 + 每信道能量探测）<br>Channel energy scan (CCA carrier sense + per-channel energy probe) | **可行**（全部 ROM 函数已列于 `rom_wifi.h`，不依赖未知签名）<br>Feasible (all ROM fns already in `rom_wifi.h`, no unknown signatures) | **推荐 / Recommended** |
| C. 主动 Beacon 探测请求扫描（`probe_req` → 收集 `probe_resp`）<br>Active beacon probe scan (probe_req → collect probe_resp) | 部分可行，依赖 RX 通路与帧回调（当前未接）<br>Partly feasible, depends on RX path + frame callback (not wired) | **探索 / Explore** |

---

## 2. 硬件事实（已核实）/ Hardware Facts (verified)

- `WDEV_BASE 0x600D0000` 在 ESP32-C6 上是 **Reserved** [TRM 5.3-2] — 802.11 MAC/BB 寄存器文件**不在公开外设地图**中。所有 Wi-Fi/BT 控制必须走片上 ROM 函数。
  `WDEV_BASE 0x600D0000` is **RESERVED** on ESP32-C6 [TRM 5.3-2] — the 802.11 MAC/BB register file is **NOT in the public peripheral map**. All Wi-Fi/BT control must go through on-chip ROM functions.
- ROM 数据指针（`kernel/include/rom_wifi.h` 注释）：`g_scan @ 0x4087ffa8`、`net80211_funcs @ 0x4087ffac`、`pp_wdev_funcs @ 0x4087ff70`、`g_chm @ 0x4087ffa4`、`g_ic_ptr @ 0x4087ffa0`、`if_ctrl_ptr @ 0x4087ff50`、`phy_param_rom @ 0x4087fce8`。
  ROM data pointers (per `rom_wifi.h` comments): `g_scan @ 0x4087ffa8`, `net80211_funcs @ 0x4087ffac`, `pp_wdev_funcs @ 0x4087ff70`, `g_chm @ 0x4087ffa4`, `g_ic_ptr @ 0x4087ffa0`, `if_ctrl_ptr @ 0x4087ff50`, `phy_param_rom @ 0x4087fce8`.
- 上述指针全部落在 HP SRAM 域（`0x40800000–0x4087FFFF`），**可直接读**（不会触发 Load access fault），可用于运行时探测 ROM 内部状态。
  All pointers fall inside the HP SRAM window (`0x40800000–0x4087FFFF`), so they are **directly readable** (no load-access fault) — usable for runtime ROM introspection.
- 内核 MPCCR 周期计数器可用：`get_mcycle()`（CSR 0x7E2，`kernel/asm/entry.S:65`），用于 CCA 驻留计时（1 cycle @ 40MHz XTAL = 25ns）。
  Kernel cycle counter available: `get_mcycle()` (CSR 0x7E2, `kernel/asm/entry.S:65`) for CCA dwell timing (1 cycle @ 40MHz XTAL = 25ns).

---

## 3. 方案 A：ROM net80211 全解调扫描 — 当前阻塞 / Full-decode ROM scan — currently blocked

### 3.1 期望行为 / Desired behavior
调用 ROM 的 `ic_start_scan` / `g_scan` 状态机，把空口 beacon/probe_resp 解调成 `struct wifi_ap_t{ssid, bssid, channel, rssi, encryption}`，填充现有 `wifisearch` 表格（`kernel/src/software.cpp:206 soft_wifisearch` 已实现渲染）。

### 3.2 阻塞原因 / Blocking reasons
| # | 阻塞点 / Blocking point | 说明 / Detail |
|---|------------------------|---------------|
| B1 | `g_ic_ptr`（IC 指针）指向的 `struct ieee80211com` 布局未知 | ESP-IDF 内部结构体，ROM 编译期写入 SRAM 顶端。无布局就不知道 `ic_scan_start`/`ic_scan_done` 偏移，无法安全调用。The layout of `struct ieee80211com` is internal to ESP-IDF; unknown layout = cannot locate scan callbacks. |
| B2 | `net80211_funcs` 表项偏移未知 | 表基址已知（0x4087ffac），但每项对应哪个函数（`ic_mac_init`/`ic_start_scan`/…）无官方对照，需逆向 ROM 数据。Table base known, but per-entry function mapping is undocumented. |
| B3 | 调用前置条件（`wdev_mac_wakeup` 已跑、MAC 已初始化）未验证 | `ROM_WIFI_IS_STARTED` 可查"是否已启动"，但启动后的回调钩子（RX/scan_done）由谁注册不明。Precondition chain (wdev_mac_wakeup, MAC init) not proven; RX/scan_done hooks unregistered. |
| B4 | doc.MD 禁止使用 ESP-IDF 组件/API | 若靠查 ESP-IDF 源码猜结构体偏移，等于"直接复制/改编公开代码 + 猜寄存器行为"，双重违规。Consulting IDF source to guess offsets would violate both "no copying" and "no conjecturing". |

### 3.3 若未来要解锁（仍需用户确认）/ If unblocking later (still needs user approval)
1. 从 ROM 的 `g_scan`/`g_ic_ptr` 运行时 dump 结构体，纯寄存器级逆向（无需 IDF 源码）；
2. 等待 wdev MAC 通路（`wdev_mac_wakeup` + `wdev_mac_reg_load`）真正跑通后，RX 帧回调才能收到 beacon；
3. 本方案当前**不投入编码**，避免浪费时间在未知 ABI 上。

---

## 4. 方案 B：信道能量扫描（CCA 载波侦听）— 推荐实现 / Channel energy scan — recommended

### 4.1 原理 / Principle
Wi-Fi 空口存在 AP 时，该信道在 CCA（Clear Channel Assessment，空闲信道评估）上表现为"信道忙"。内核可以在**每个信道**停留固定时间，用 CCA 能量检测判断"该信道是否有信号"。这回答"**周围有没有 Wi-Fi、在哪些信道**"（粗粒度但真实），且**不需要解调任何帧**。

### 4.2 依赖的 ROM 函数（全部已列于 `rom_wifi.h`，无未知签名）/ Required ROM functions (all in `rom_wifi.h`, no unknown signatures)
| ROM 函数 / Function | 地址 / Addr | 用途 / Use |
|---|---|---|
| `wifi_rf_phy_enable` | `0x40000BA8` | 开 RF/PHY（PHY 初始化的前置） / enable RF |
| `phy_param_rom` | `0x4087fce8` | PHY 参数表 / PHY params |
| `set_chan_reg` | `0x4000113C` | 切信道寄存器 / set channel (reg) |
| `set_channel_rfpll` | `0x4000123C` | 切信道 RFPLL（含频点设置） / set channel (RFPLL) |
| `set_cca` | `0x40001210` | CCA 阈值配置 / CCA threshold |
| `set_rx_sense` | `0x40001214` | RX 灵敏度（能量检测阈值） / RX sense threshold |
| `phy_enable_cca` / `phy_disable_cca` | `0x40001344` / `0x40001340` | 开/关 CCA / enable/disable CCA |
| `read_hw_noisefloor` | `0x400013A0` | 读硬件噪声底（每信道能量参考） / HW noise floor |
| `wdev_get_noise_floor` | `0x40000DDC` | 取噪声底（wdev 封装） / get noise floor |
| `enable_agc` / `disable_agc` | `0x4000133C` / `0x40001338` | AGC（能量检测前必须开） / AGC |
| `wifi_rf_phy_disable` | `0x40000BA4` | 收尾关闭 / shutdown |

### 4.3 候选信号强度读取路径 / Candidate signal-strength read paths
| 路径 / Path | 做法 / Method | 风险 / Risk |
|---|---|---|
| B-P1 直接读噪声底 | 每信道 `read_hw_noisefloor` / `wdev_get_noise_floor`，与无信号信道（1/14 边缘）对比 | 高 — 噪声底主要由硬件偏移决定，**不代表**信道上有 AP |
| B-P2 CCA 忙计数 | 每信道驻留 N ms，统计 CCA 由闲→忙的占比；有 AP 的信道忙占比显著更高 | 中 — 需要一个可读的 CCA 状态位（寄存器），需在 TRM 找到并验证 |
| B-P3 误码率 / 解调失败率 | 不适用于被动能量扫描 | — |

> 结论：**B-P2（CCA 忙计数）** 是主路径；若 CCA 状态寄存器读不到，退化为 **B-P1** 作为"是否有信号"的参考。由于 doc.MD 禁止猜测寄存器地址，B-P2 要求先从 TRM 找到 CCA 状态位；**找不到就先只做 B-P1**（把噪声底当粗能量指示，明确标注不精确）。

### 4.4 与当前代码的接口 / Interface with existing code
- 新增函数：`wifi_chan_scan(struct wifi_ap_t* results, int max_results)`，放进 `kernel/src/wifi.cpp`，在 `wifi.h` 声明。
- 复用它复用现有 `wifisearch` 渲染（`software.cpp` 的 `soft_wifisearch` 已打印 SSID/BSSID/CH/RSSI/ENC 表）。能量扫描只填 `channel` + `rssi`（能量），`ssid/bssid` 留空，渲染时显示为 `<energy>` / 0。
- 前置条件：`g_wifi_powered`（modem 域上电，`modem_power_up` 在 `wifi_drv_init` 已跑）；扫描开始时自动调用 `wifi_phy_calibrate()`（即现有 `phy_init_sequence()`，27 步 ROM 校准）。
- 需要自旋等待工具：`get_mcycle()` 差值换算微秒/毫秒（`CYCLES_PER_US=40` @ 40MHz XTAL，`kernel.h:177`）。

### 4.5 里程碑 / Milestones
| 步 / Step | 内容 / Work | 出口检查 / Exit check |
|---|---|---|
| B-M1 | `wifi_phy_calibrate()` 从 shell 可调用（新命令或复用 `wifi -cal`） | PHY 校准日志出现、无 trap |
| B-M2 | 每信道切换 `set_channel_rfpll` + 驻留，读噪声底/能量 | 输出 1–14 信道能量表 |
| B-M3 | 接入 `soft_wifisearch` 渲染 | `wifisearch` 显示各信道能量 |
| B-M4 | 找 TRM 中的 CCA 状态位 → 忙计数 | 有 AP 的信道忙占比明显高 |

### 4.6 风险 / Risks
- PHY 校准在未运行 ESP-IDF 初始化的情况下可能不完整 → 能量读数可能偏低或恒定（设计上接受，作为"粗探测"）。
- 每个信道的驻留需要毫秒级延时，shell 任务会阻塞（协作式调度下其他任务暂停，可接受——`ping` 已如此）。
- 读到的是**能量**，不是 RSSI：与真实 RSSI 的换算需校准，第一版不承诺。

---

## 5. 方案 C：主动探测请求扫描 / Active probe-req scan

- 原理：发送 `probe_req`（全 0xFF 广播 + SSID wildcard），AP 回 `probe_resp`，解调 `probe_resp` 得 SSID/BSSID/RSSI。
- 阻塞点：**发送和接收都要走 wdev MAC 通路**（`wdev_mac_wakeup`/`wdev_mac_reg_load` + PP 层的 RX 帧回调），当前 RX 通路未接通（`wifi_drv_read` 返回 -1）。此方案依赖方案 A 的 net80211 栈，**优先级低于方案 B**。
- 现状结论：C 是最终形态（能拿到 SSID/BSSID/加密类型），但依赖 B 无法覆盖的 TX/RX 通路。**列为探索项**，等 MAC 通路证据（`ROM_WIFI_IS_STARTED`、`wdev_mac_wakeup` 返回值）出现后再决定。

---

## 6. 已存在的诚实状态模型（不可破坏）/ Existing honest state model (do not break)

`kernel/src/wifi.cpp` 用 mac80211 概念维护状态机：
| 状态 / State | 含义 / Meaning | mac80211 对应 |
|---|---|---|
| `g_wifi_powered` | modem 域上电 + APB 时钟（硬件真值） | `op .start` |
| `g_wifi_ap_started` | AP 已配置（SSID/CH 已设） | `op .start_ap called` |
| `g_wifi_ap_on` | AP 真正在空口发信标 | `op .start_ap done` |

- `wifi_is_active()` 只在 `g_wifi_ap_on` 为真时返回真（诚实语义：不发信标的 AP 不叫 active）。
- `wifi_scan()` 当前返回 0 + "not supported yet (ROM scan path not wired)"。
- **新增能量扫描不能破坏这套模型**：能量扫描是只读探测，不改 `g_wifi_ap_started/g_wifi_ap_on`。

---

## 7. 下一步动作 / Next actions

1. **（推荐）实现方案 B 的 B-M1**：在 `wifi.cpp` 暴露 `wifi_chan_energy_scan()`，先验证"切信道 + 读噪声底/CCA"在硬件上不 trap、有变化。
2. 同时给 shell 加一条调试命令（如 `wifichan` 或 `wifi -chan`），把 1–14 信道能量表打印出来。
3. 若能量无任何信道差异 → 记录为"能量扫描不可区分信号"，退回方案 A/C 的 ROM 逆向路径，并在本文更新结论。
4. 所有寄存器/ROM 地址改动前查 TRM；本文按 doc.MD 规则由用户确认后更新。

---

## 附：本次调研阅读过的文件 / Files reviewed during this analysis

| 文件 / File | 用途 / Relevance |
|---|---|
| `AI/doc.MD` | 最高优先级准则：无 IDF、禁止猜寄存器、禁止复制公开代码 |
| `AI/how_to_read.MD` | 陷阱 #11（被注释的 ROM 函数可能仍存在）、#14（RTS=复位） |
| `kernel/include/rom_wifi.h` | ROM 函数地址全集 + ROM 数据指针注释 |
| `kernel/src/wifi.cpp` | 现有 PHY 序列、状态机、`wifi_scan()` 桩 |
| `kernel/src/software.cpp` | `soft_wifisearch` 渲染层（扫描结果表） |
| `kernel/src/console.cpp` | `cmd_wifi`/`wifisearch` 命令分发 |
| `kernel/src/main.cpp` | 启动顺序（`drivers_init` → shell/gui 任务） |
| `kernel/src/bt.cpp` | BT 驱动（同类 ROM 驱动，参考状态模型） |
| `kernel/asm/entry.S` | `get_mcycle()`（CSR 0x7E2）计时来源 |
| `kernel/Makefile` | 构建系统（`riscv64-unknown-elf-g++`） |
