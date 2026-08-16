# ESP32_OS 代码库阅读指南 — 给 AI 的说明

> **维护规则**：新增、删除或重命名文件时，必须同步更新本文件中的「代码文件用途」章节。修改功能时，必须同步更新对应文件的用途描述和「常见陷阱」章节。保持本文件与代码库的实际状态一致。

## 基本准则

1. **`AI/doc.MD` 是最高优先级准则** — 所有决策以 doc.MD 为准，prompt 文件次之
2. **无 ESP-IDF** — 所有代码直接操作硬件寄存器，禁止使用 ESP-IDF API
3. **寄存器地址必须对照 TRM 验证** — 禁止猜测寄存器地址或指令行为
4. **参考 Linux 内核源码** (`/media/ssd/linux-7.0.1`) — 仅限设计概念参考，禁止复制代码
5. **C++ 编译** — 所有 `.cpp` 文件用 `riscv64-unknown-elf-g++` 编译，freestanding 模式，无 exceptions/RTTI
6. **ESP32-C6 硬件** — RV32IMAC @ 160MHz, 512KB HP SRAM, 40MHz XTAL, Direct Boot 模式
7. **UART 寄存器位域** — ESP32-C6 的 UART_STATUS 寄存器中，TX FIFO 计数在 bits [23:16]，RX FIFO 计数在 bits [7:0]（与某些文档可能相反，以实际硬件行为为准）
8. **Wi-Fi / BT 使用芯片内置 ROM 函数** — ESP32-C6 的 Wi-Fi / BT PHY 和 MAC 初始化通过调用芯片内置 ROM 函数完成（地址定义在 `kernel/include/rom_wifi.h`），无需 ESP-IDF 固件 blob。ROM 函数地址来自 ESP-IDF v6.0.2 的 LD 链接脚本，经交叉验证

---

## 代码文件用途

### 引导加载程序 (`boot/`)

| 文件 | 用途 |
|------|------|
| `boot/asm/start.S` | ESP32-C6 引导加载程序入口点 — Direct Boot 模式，硬件初始化（UART、WDT、SPI Flash），密码验证，内核加载和校验，控制转移 |
| `boot/include/reg_c6.inc` | ESP32-C6 寄存器定义 — UART、PCR、WDT、SHA、SPI1 等外设寄存器地址和位域 |
| `boot/include/boot_api.inc` | 引导-内核接口定义 — boot_params_t 结构体、mailbox_t 结构体、密码 API 的汇编常量 |
| `boot/include/kernel_hdr.inc` | 内核镜像头格式 — 魔数、入口点、加载地址、CRC32 校验等字段偏移 |
| `boot/linker.ld` | 引导加载程序的链接脚本 |
| `boot/tools/flash.py` | 烧录工具 — 将 bootloader + kernel 写入 flash |
| `boot/tools/mkkernel.py` | 内核镜像打包工具 |
| `boot/tools/mkpasswd.py` | 密码哈希生成工具 |

### 内核 (`kernel/`)

| 文件 | 用途 |
|------|------|
| `kernel/asm/entry.S` | 内核入口点 — 设置栈、清 BSS、启用性能计数器、安装陷阱处理程序、调用 `kernel_main()`；包含 `get_mcycle()` 和 `_kern_trap` |
| `kernel/asm/ctxsw.S` | 协作式上下文切换 — `ctx_switch()` 保存/恢复 callee-saved 寄存器 (ra, s0-s11) |
| `kernel/include/kernel.h` | 内核公共 API — 类型定义、寄存器地址、驱动接口、调度器、VFS、内存管理、IPC 等声明 |
| `kernel/include/gui.h` | GUI 子系统公共 API — 帧缓冲、绘图原语、字体、窗口管理器、事件、控件声明 |
| `kernel/linker.ld` | 内核链接脚本 — 注意 `.sbss` 和 `.sdata` 节必须包含在 BSS/data 中，否则堆与 BSS 变量重叠 |
| `kernel/src/main.cpp` | `kernel_main()` — 初始化顺序：内存管理→VFS→环境变量→控制台→ESPSYS→驱动→调度器→任务 |
| `kernel/src/console.cpp` | 控制台 — UART I/O (`uart_putc/uart_getc/uart_avail`)、kprintf、shell 命令分发表、环境变量、用户管理、管道 |
| `kernel/src/espsys.cpp` | 系统调用 — 驱动框架 (`driver_register/driver_find`)、显示模式管理、ESPSYS 调用表 |
| `kernel/src/mm.cpp` | 内存管理器 — 首次适应空闲链表分配器，支持拆分和合并 |
| `kernel/src/sched.cpp` | 协作式调度器 — 任务创建/切换/退出、轮转调度、空闲任务、消息队列 IPC |
| `kernel/src/vfs.cpp` | 虚拟文件系统 — 内存文件系统，支持目录/文件创建、读写、删除、重命名；节点迭代 API（`vfs_node_count/vfs_node_get/vfs_path_of`）供 `find` 等遍历整棵树 |
| `kernel/src/partition.cpp` | 分区表管理 — 内存中的分区配置，系统分区保护（双确认） |
| `kernel/src/pki.cpp` | 包管理器 — 安装/移除/打包 `.espapp` 格式的软件包 |
| `kernel/src/software.cpp` | 软件工具套件 — ping、wifisearch、wifiinfo、dhcp、track、curl、desktop（图形化桌面：菜单栏 System/Apps、彩色图标网格（计算式均匀布局）、下拉菜单、Start 菜单、模态窗口、**交互式包管理器窗口**（列出 /app 下已装包、选中并删除，root 限制）；**每个窗口共用 `desktop_window_frame()` 边框+标题栏+右上角红色 [X] 关闭按钮**，File Manager 是**可交互 VFS 浏览器**（箭头/WASD/Enter 进入目录、[..]/Backspace 上一级、鼠标点击行，状态行实时显示选中文件/目录信息）；支持鼠标点击+悬停与键盘）、edit 编辑器 |
| `kernel/src/coreutils.cpp` | 核心实用命令（基于 VFS/MM，无寄存器/IDF）— `cat`、`rm`、`cp`、`clear`、`whoami`、`free`、`hexdump`、`wc`、`find`（用 VFS 迭代 API 遍历整棵树）、`calc`（整数递归下降求值）、`sleep`、`date`（无 RTC，基于开机计数） |
| `kernel/src/net.cpp` | SLIP 网络驱动 — 基于 UART 的 SLIP 帧收发、IP 包处理、ICMP 回显回复 |
| **Wi-Fi / BT 驱动** | |
| `kernel/include/wifi.h` | Wi-Fi / BT 驱动头文件 — WDEV 子系统寄存器定义（0x600D0000 基址）、AP 扫描结果结构体、公共 API 声明 |
| `kernel/include/rom_wifi.h` | ESP32-C6 ROM 函数地址表 — PHY、net80211、PP 三大模块的 ROM 函数地址（来自 esp32c6.rom.*.ld）、类型转换辅助函数 |
| `kernel/src/wifi.cpp` | Wi-Fi 驱动实现 — ROM 函数 PHY 初始化序列（26 步校准）、MAC 地址设置、信道配置、AP 模式（信标间隔 100TU）、扫描 |
| `kernel/src/bt.cpp` | BLE 驱动实现 — ROM 函数 BT PHY 校准、BT 时钟控制、MAC 地址（= WiFi MAC+1）、设备扫描 |
| **GUI 子系统** | |
| `kernel/src/gui/gui_core.cpp` | GUI 核心 — 启动/停止、事件循环、性能基准测试 |
| `kernel/src/gui/gui_draw.cpp` | 绘图引擎 — 像素、Bresenham 线/圆、矩形、位图、填充 |
| `kernel/src/gui/gui_font.cpp` | 字体渲染 — 8×16 等宽点阵字体（ASCII 95 字符，1520 字节） |
| `kernel/src/gui/gui_fb.cpp` | 帧缓冲管理 — 双缓冲分配/交换/释放 |
| `kernel/src/gui/gui_window.cpp` | 窗口管理器 — 创建/销毁/移动/焦点/Z序、事件队列、渲染 |
| `kernel/src/gui/gui_cmd.cpp` | GUI 命令 — `gui -start/-stop/-list/-kill/-bench` |
| `kernel/src/gui/gui_widget.cpp` | 基础控件 — 按钮、标签 |
| `kernel/src/gui/gui_lcd_spi.cpp` | SPI LCD 驱动框架（空桩 — 无物理 LCD） |
| `kernel/src/gui/gui_touch_i2c.cpp` | I2C 触摸驱动框架（空桩 — 无物理触摸） |
| `kernel/tools/pack_kernel.py` | 内核打包工具（添加 CRC32 校验头） |

### 监视器 (`monitor/`)

| 文件 | 用途 |
|------|------|
| `monitor/monitor.cpp` | GTK3 串口监视器（C++，唯一版本）— 实时控制台、设备信息面板、命令补全（Tab + 下拉）与输入历史（↑↓）。`desktop` 模式用独立的 Cairo 图形画布渲染（16 色调色板 + 真实单元格）。**鼠标交互**：画布上的点击/移动被转换为自定义 CSI 转发给设备——`ESC[<e>;<row>;<col>M`（e=0 悬停、1 点击，行列从 1 起）；悬停经 ~30ms 节流+按单元格去重以限制串口流量。方向键/回车/Esc 也原样转发（键盘仍可用）。复位用 RTS 脉冲、DTR 保持拉低（见陷阱 #14）。用 `make` 构建（g++ 不可用时自动回退 clang++）。**注意**：桌面绘制必须是单字节 ASCII（画布一字节=一单元格，UTF-8 制表符会乱码）；ANSI 解析器 `parse_csi` 早期有 off-by-one，已修复。Python 版 `monitor.py` 已删除。 |

---

## 系统提示词摘要

### 引导 (`AI/prompt/boot/`)

| 文件 | 摘要 |
|------|------|
| `AI/prompt/boot/prompt_en.MD` | **主参考** — ESP32-C6 引导加载程序开发规范：硬件初始化→执行环境→调试输出→内核加载→控制转移→API 接口→引导模式→紧急模式 |
| `AI/prompt/boot/prompt_cn.MD` | 中文版，供英文理解困难时辅助参考 |

### 内核 (`AI/prompt/kernel/`)

| 文件 | 摘要 |
|------|------|
| `AI/prompt/kernel/prompt_en.MD` | 内核功能扩展与结构优化：最小化、模块化、可扩展性、可靠性、性能优先；覆盖内存管理、任务调度、IPC、VFS、驱动框架 |
| `AI/prompt/kernel/prompt_cn.MD` | 中文版 |
| `AI/prompt/kernel/update2_en.MD` | 内核更新 v2：添加了分区表管理、PKI 包管理器、软件工具套件（ping/curl/desktop/edit/track）、用户管理、环境变量 |
| `AI/prompt/kernel/update2_cn.MD` | 中文版 |
| `AI/prompt/kernel/update_prompt_en.MD` | 内核更新补充：调度器改进、IPC 消息队列、SLIP 网络驱动 |
| `AI/prompt/kernel/update_prompt_cn.MD` | 中文版 |

### GUI (`AI/prompt/GUI/`)

| 文件 | 摘要 |
|------|------|
| `AI/prompt/GUI/prompt_en.MD` | GUI 子系统开发规范：帧缓冲→SPI LCD→触摸→绘图引擎→字体→窗口管理器→控件→shell 集成，P0/P1/P2 优先级 |
| `AI/prompt/GUI/prompt_cn.MD` | 中文版 |
| `AI/prompt/GUI/prompt.MD` | 混合中英文版 |

### 软件工具 (`AI/prompt/software/`)

| 文件 | 摘要 |
|------|------|
| `AI/prompt/software/prompt_en.MD` | 软件工具开发规范：每个工具打包为 `.espapp` 格式，通过 PKI 管理器动态加载 |
| `AI/prompt/software/prompt_cn.MD` | 中文版 |

### 用户程序 (`AI/prompt/user/`)

| 文件 | 摘要 |
|------|------|
| `AI/prompt/user/prompt_en.MD` | 用户程序开发规范：运行于内核之上，不依赖 IDF |
| `AI/prompt/user/prompt_cn.MD` | 中文版 |

---

## 项目计划文件 (`AI/AI_project/`)

| 文件 | 摘要 |
|------|------|
| `AI/AI_project/boot_plan.md` | 引导加载程序开发计划（Phase 1） |
| `AI/AI_project/kernel_plan.md` | 内核开发计划（Phase 2） |
| `AI/AI_project/system_plan.md` | 系统整体计划（Phase 3）：内核子系统 + 用户工具 |
| `AI/AI_project/update3_commands_desktop_en.md` | Update 3 设计说明：新增 12 个核心命令（coreutils.cpp）、桌面图标计算式均匀布局、交互式包管理器窗口 |
| `AI/AI_project/think/` | 技术调研与可行性分析（调研/猜测，不直接编码）；`think/README.md` 为索引 |

---

## 关键依赖关系

```
boot/asm/start.S (引导加载程序)
    ↓ 控制转移 (a0 = boot_params_t*)
kernel/asm/entry.S (内核入口)
    ↓ 调用 kernel_main()
kernel/src/main.cpp
    ├── kernel/src/mm.cpp      (内存管理)
    ├── kernel/src/vfs.cpp     (虚拟文件系统)
    ├── kernel/src/console.cpp (控制台 + shell)
    ├── kernel/src/espsys.cpp  (驱动框架)
    ├── kernel/src/sched.cpp   (调度器)
    ├── kernel/src/net.cpp     (SLIP 网络驱动)
    ├── kernel/src/wifi.cpp    (Wi-Fi 驱动, ROM PHY)
    ├── kernel/src/bt.cpp      (BLE 驱动, ROM BT)
    ├── kernel/src/software.cpp (软件工具)
    └── kernel/src/gui/        (GUI 子系统)
```

## 常见陷阱

1. **`.sbss` 节** — 链接脚本必须包含 `*(.sbss .sbss.*)`，否则 `g_task_count`、`g_heap_head` 等小 BSS 变量不会被清零，导致堆与 BSS 重叠
2. **UART 寄存器位域** — TX FIFO 计数在 bits [23:16]，RX FIFO 计数在 bits [7:0]（`uart_putc` 读 bits [23:16]，`uart_avail` 读 bits [7:0]）
3. **`wfi` 门控 UART 时钟** — 空闲任务中的 `wfi` 指令会门控 UART 时钟，导致 UART RX 无法接收数据。`sched.cpp` 的 `idle_task()` 因此使用忙等待而非 `wfi`
4. **IO_MUX / GPIO 基址容易搞错** — `IO_MUX_BASE = 0x60090000`，`GPIO_BASE = 0x60091000`（两者相邻，写错会静默破坏其他引脚）。每引脚寄存器**不是** `base + pin*4`：GPIO16(U0TXD) 在 `base+0x44`，GPIO17(U0RXD) 在 `base+0x48`
5. **UART0 引脚的 MCU_SEL = 0** — `MCU_SEL` 位于 bits [14:12]（不是 bit 0），值 0 = U0TXD/U0RXD，1 = 普通 GPIO，2 = FSPICS。RX 引脚还必须设置 `FUN_IE`(bit 9) 输入使能和 `FUN_PU`(bit **8**，bit 7 是 FUN_PD 下拉，不可误设)，否则 RXFIFO 永远为 0
6. **`task_start()` 不可用 `task_yield()` 启动** — 首次切换必须把 `kernel_main` 的上下文存入一个丢弃变量，不能存进 `g_tasks[0].sp`（那里已是 `idle_task` 的预置栈帧）。否则 idle 会以 `kernel_main` 恢复并返回 `entry.S` 的 `_kern_halt: wfi`，门控 UART 时钟使 shell 只回显一次后彻底失去输入
7. **`kprintf` 支持的格式** — 支持 `%s %d %u %x %c %%`，并支持可选的标志与宽度：`-`（左对齐）、`0`（补零，仅右对齐数字）、十进制宽度。例如 `%08x`、`%02x`、`%4d`、`%03u`、`%-6s` 均可用。不支持精度（`%.3f`）、`%ld`、`%f`。未知转义会原样打印且**不消耗**可变参数。（历史陷阱：旧版仅支持 `%08x/%02x` 两种硬编码宽度，其它宽度如 `%2d/%03u` 会错位吞参；现已在 `console.cpp:kprintf` 通用化修复。）
8. **CRLF 产生重复提示符** — 终端发送 `\r\n` 两个字节，`console_readline` 用静态 `last_was_cr` 吞掉配对的 LF，否则每条命令都会多打印一个提示符
9. **`get_mcycle()` 使用 CSR 0x7E2** — ESP32-C6 使用自定义 CSR MPCCR (0x7E2)，而非标准 `mcycle` (0xB00)
10. **窗口 `int16_t` 坐标** — `window_t.x/y` 是 `int16_t`，转换为 `uint16_t` 时负值会环绕，触摸事件分发的坐标比较需要处理
11. **Wi-Fi ROM RF/PHY 函数会复位芯片** — 经实测（2026-08-03，真机 ESP32-C6）：裸机直接调用 ROM 的 RF/PHY 函数（如 `wifi_rf_phy_enable` 0x40000BA8、`set_channel_rfpll` 0x4000123C）会**立即复位芯片**（表现为回到 `kernel_main` 报 "boot_params invalid"，非干净 trap）。这些是 ROM 主初始化器 `register_chipv7_phy(phy_init_data,...)` 的子函数，缺少 ESP-IDF 的 `phy_init_data` blob 注册就会 fault。仅 `wdev_get_noise_floor` (0x40000DDC) 可安全调用（返回 0）。结论：doc.MD 约束下**没有可用的 Wi-Fi 扫描/信标路径**。`wifi.cpp` 的 `phy_init_sequence()` 只能经 `wifi -cal force` 显式复现，禁止放入任何自动初始化路径。详见 `AI/AI_project/think/01_wifi_scan_feasibility_en.md` §6b。
12. **Wi-Fi AP 模式信标模板** — 信标帧通过 WDEV MAC 硬件自动生成，模板在 `wifi.cpp` 中硬编码。如果 SSID 长度变化需要重新设置 WDEV_SSID_DATA 寄存器组
13. **蓝牙地址** — BLE MAC 地址 = Wi-Fi MAC + 1（bt.cpp: `g_bt_mac[5]++`）。此偏移与 ESP-IDF 的行为一致
14. **开发板复位靠 RTS，不是 DTR** — 本机 CH343 板：RTS → EN(复位)，DTR → GPIO9(启动模式选择)。DTR 必须保持 False，拉低会进入 ROM 下载模式。用 DTR 复位会完全没有启动输出，极易误判为 UART 或固件故障
15. **桌面鼠标协议是跨文件约定** — 桌面由内核（`software.cpp`）单方绘制并处理全部逻辑；监视器只渲染 ANSI 并把画布鼠标事件转成自定义 CSI `ESC[<e>;<row>;<col>M`（e=0 悬停/1 点击）发回设备。内核 `desktop_read_event()` 的解析器与监视器 `desktop_send_mouse()` 的格式必须保持一致——改一处必须同步改另一处。热区命中用 `software.cpp` 的 `g_regions[]`（每次全量重绘时重建，后注册者优先，故下拉菜单/窗口覆盖图标）。图标位置由索引**计算**（`icon_row/icon_col`，网格常量 `GRID_COLS/TILE_W/ICON_GAP_*`），增删图标无需手改坐标。模态窗口用 `modal` 变量区分：1=文件 2=关于 3=网络 4=设置 **5=包管理器（交互式，有自己的选择/删除分支，不走"任意点击关闭"）**。**每个窗口都经 `desktop_window_frame(top,col,W,h,title)` 统一画边框+标题栏，右上角红色 `[X]` 注册 `ACT_CLOSE` 热区**——所有窗口（含文件/包管理器）的关闭键统一为 `[X]`、Esc、`q`、点击窗口外；改动窗口布局必须保持标题栏在 `top+1` 行、`[X]` 在 `col+W-4..col+W-2`（列 `col+W-4` 起 3 格，否则热区与视觉错位）。文件管理器列表行注册 `ACT_FM_ROW0+i`（≥400），包管理器行注册 `ACT_PKI_ROW0+i`（≥200），这些基线不可与 `ACT_*` 枚举（<100）冲突。所有桌面绘制必须是单字节 ASCII（画布一字节=一单元格；`+ - |` 画边框，禁用 UTF-8 制表符）
16. **烧录/串口失联的物理恢复** — 若 `esptool` 反复报 "device reports readiness to read but returned no data" / "No serial data received"，且 `serialtest.py` 复位后抓到 0 字节、`fuser /dev/ttyACM0` 显示无进程占用，则是 CH343 CDC 需要**物理重新插拔 USB**（或按板上 reset 键）才能重新枚举。纯软件复位（RTS 脉冲、强制 download strap）都无法恢复这种 CDC 挂死状态