# ESP32_OS

基于 ESP32-C6 (RISC-V RV32IMAC) 的嵌入式操作系统，**不依赖 ESP-IDF**，所有代码直接操作硬件寄存器。

## 硬件平台

| 参数 | 值 |
|------|------|
| MCU | ESP32-C6 (RISC-V 32-bit) |
| SRAM | 512KB HP SRAM + 16KB LP SRAM |
| 启动方式 | Direct Boot (ROM → bootloader → kernel) |
| 时钟 | 40MHz 外部晶振 |

## 目录结构

```
ESP32_OS/
├── boot/          # 一级引导程序 (bootloader)
├── kernel/        # 内核
│   ├── asm/       # 汇编代码 (entry.S, ctxsw.S)
│   ├── src/       # C++ 源码
│   ├── include/   # 头文件
│   └── tools/     # 构建工具
├── monitor/       # 监控终端工具
└── AI/            # 设计文档和提示词
```

## 支持的命令

### 文件管理

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `ls` | 列出当前目录文件 | `ls` |
| `mkdir` | 创建目录 | `mkdir newdir` |
| `newfile` | 创建文件 | `newfile test.txt` |
| `cd` | 切换目录 | `cd /system` |
| `pwd` | 显示当前路径 | `pwd` |
| `rename` | 重命名文件/目录 | `rename old new` |

### 系统信息

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `help` | 显示命令列表（精简版） | `help` |
| `cmd -list` | 列出所有已注册命令 | `cmd -list` |
| `cmd -unregister <name>` | 删除第三方命令（系统命令受保护） | `cmd -unregister mycmd` |
| `device` | 显示设备信息 | `device` |
| `info` | 显示启动参数 | `info` |
| `disk` | 磁盘信息（`-free`/`-part`/`-parted`） | `disk -free` |
| `part -list` | 列出闪存分区表 | `part -list` |
| `part -add <name>` | 添加分区 | `part -add data` |
| `part -del <name>` | 删除分区（系统分区需双重确认） | `part -del data` |
| `drivers` | 列出已注册驱动 | `drivers` |
| `log` | 显示启动日志 | `log` |
| `version` | 显示内核版本 | `version` |
| `uptime` | 显示系统运行时间 | `uptime` |
| `ps` | 显示任务列表 | `ps [-a] [-l]` |

### 环境变量

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `export VAR=value` | 设置环境变量 | `export PATH=/usr/bin` |
| `export VAR` | 查看已导出的变量 | `export PATH` |
| `export -n VAR` | 删除环境变量 | `export -n TEMP` |
| `env` | 列出所有环境变量 | `env` |
| `echo $VAR` | 打印变量值 | `echo $PATH` |

### 用户管理

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `user -add <name>` | 创建用户 | `user -add alice` |
| `user -del <name>` | 删除用户（需root） | `user -del alice` |
| `user -per <name> <root\|user>` | 修改权限（需root） | `user -per alice root` |
| `su <name>` | 切换用户 | `su alice` |

### 网络与连接

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `wifi` | Wi-Fi 信息与控制 | `wifi -on` |
| `net` | 网络连接状态 | `net` |
| `com` | 串口连接状态 | `com` |
| `btscan` | 扫描蓝牙设备 | `btscan` |
| `wifisearch` | 扫描 Wi-Fi 接入点 | `wifisearch` |
| `wifiinfo` | 显示当前 Wi-Fi 详情 | `wifiinfo` |

### 命令管理

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `cmd -list` | 列出所有命令（含系统/用户标记） | `cmd -list` |
| `cmd -unregister <name>` | 移除第三方命令 | `cmd -unregister mycmd` |
| `chdiv` | 更改参数分隔符 | `chdiv "/"` |

### 应用工具

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `pki -i <file>` | 安装 .espapp 软件包 | `pki -i app.espapp` |
| `pki -r <name>` | 移除已安装软件包 | `pki -r myapp` |
| `pki -m <dir>` | 打包目录为 .espapp | `pki -m /app/myapp` |
| `ping <host>` | ICMP 网络测试 | `ping 8.8.8.8` |
| `dhcp` | DHCP 状态查询 | `dhcp status` |
| `track` | 网络安全渗透工具 | `track` |
| `curl <url>` | HTTP 请求工具 | `curl http://example.com` |
| `desktop` | 简易桌面环境 | `desktop` |
| `edit <file>` | 文本编辑器 | `edit test.txt` |
| `echo <text>` | 打印文本或变量 | `echo $HOME` |

### 系统控制

| 命令 | 功能 | 用法示例 |
|------|------|----------|
| `reboot` | 重启设备 | `reboot` |
| `mode` | 设置显示模式 | `mode serial` |
| `chdiv` | 更改参数分隔符 | `chdiv "/"` |

## 通用用法

- 所有命令后加 `-help` 可查看详细帮助：`ls -help`、`part -help`
- 默认参数分隔符为空格，可通过 `chdiv` 更改
- 管道 `|` 支持：`cmd1 | cmd2`
- 环境变量引用：`echo $PATH`

## 构建

```bash
# 构建 bootloader
cd boot && make

# 构建 kernel
cd kernel && make

# 烧录（需连接 ESP32-C6）
cd kernel && make flash

# 串口监视
cd kernel && make monitor
```

## 启动流程

1. **ROM** → 检查 flash 偏移 0x00 的 Direct Boot magic (0xaedb041d)
2. **Bootloader** → 初始化硬件，验证 kernel CRC，跳转到 kernel
3. **Kernel** → 初始化内存管理器、VFS、环境变量、控制台、驱动
4. **调度器** → 启动协作式多任务（shell + monitor + idle 任务）

## 许可证

MIT