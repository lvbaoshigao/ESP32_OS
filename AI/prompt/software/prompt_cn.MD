
## 全局要求
- 使用 C 语言开发，不要使用现有API，根据内核开发
- 每个工具打包成一个独立的 `.espapp` 文件（本质是带自描述头的 ELF 重定位文件或扁平的函数表结构），由内置的“PKI 工具”动态加载并注册到命令树中。
- PKI 是内核级包管理器，提供 `pki install <name.espapp>` 和 `pki remove <name>` 命令，安装时解析 ESPAPP 头并集成到主控制台。

## 需实现的软件集及详细规格

### 1. ping
- 用法完全兼容 Linux ping，支持 `ping <ip/domain> [-c count] [-i interval]`。
- 基于 lwip raw API 构造 ICMP echo 请求，解析回复，计算延迟和丢包率。
- 域名解析依赖 lwip DNS。

### 2. wifisearch
- 扫描周围 Wi-Fi 接入点，输出列表，包含 SSID、MAC、信道、RSSI、加密类型。

### 3. wifiinfo
- 显示当前连接的 Wi‑Fi 详细信息：SSID、MAC、信道、信号强度（dBm）、加密类型、协商速率、本机 IP 等。
- 未连接时提示无连接。

### 4. DHCP
- 说明用法：连接互联网后 DHCP 会自动启用分配 IP 流程。
- 提供 `dhcp status` 命令可显示当前 IP、子网掩码、网关、DNS 服务器，基于 `esp_netif` 查询。

### 5. Track —— 网络安全渗透工具
- 输入 `Track` 后进入子环境，提示符变为 `Track/$`。
- 包含子工具：
  - **DEAUTH_tools**：进入后提示符变为 `Track/DEAUTH:$`，输入 `help` 显示帮助列表。支持指定目标 AP 和客户端发送 deauth 帧（基于 `esp_wifi_80211_tx`）。
  - **Zigbee 嗅探**：基于 IEEE 802.15.4 硬件，将 Zigbee 帧通过串口输出或保存。
  - **信道切换**：快速切换当前 Wi‑Fi 信道以配合监听（`esp_wifi_set_channel`）。

### 6. curl
- 用法与传统 curl 一致：`curl <url> [-X method] [-H header] [-d data]`。
- 使用 `esp_http_client` 实现，支持 GET、POST 等，输出响应体和状态码。

### 7. Desktop —— 简易桌面环境
- **背景**：纯蓝色。
- **桌面**：极简，默认只有一个文件管理器图标（可打开文件列表）。
- **开始菜单**：包含关机、重启选项。
- **关机/重启**：调用 "kernel API" 和深度睡眠或断电提示（实际由外部电路控制，这里提供软件关机提示）。


### 8. EDIT —— 文本编辑工具
## ESPAPP 打包与 PKI 集成
- 每个工具编译为一个独立的组件，最终生成 `.espapp` 文件。头结构包含：工具名、入口函数地址、版本、依赖列表。
- PKI 工具解析该头后，将入口函数注册到主命令表，实现动态安装。
- 卸载时从命令表中移除并释放相关资源。

请生成所有工具的完整 C 源码及对应的 CMakeLists.txt