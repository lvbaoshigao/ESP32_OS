## Global Requirements
- Use C ,don't use IDF API
- Package each tool as an independent `.espapp` file (essentially a relocatable ELF file with a self-describing header, or a flat function table structure). A built-in "PKI tool" dynamically loads and registers it into the command tree.
- PKI is a kernel-level package manager that provides the commands `pki install <name.espapp>` and `pki remove <name>`. During installation, it parses the ESPAPP header and integrates the tool into the main console.

## Tool Suite and Detailed Specifications

### 1. ping
- Usage must be fully compatible with Linux ping: `ping <ip/domain> [-c count] [-i interval]`.
- Use lwIP raw API to construct ICMP echo requests, parse replies, and calculate latency and packet loss.
- Domain name resolution relies on lwIP DNS.

### 2. wifisearch
- Scan for nearby Wi‑Fi access points and output a list including SSID, MAC, channel, RSSI, and encryption type.

### 3. wifiinfo
- Display detailed information about the current Wi‑Fi connection: SSID, MAC, channel, signal strength (dBm), encryption type, negotiated rate, local IP, etc.
- If not connected, indicate that there is no connection.

### 4. DHCP
- Description of usage: After connecting to the internet, DHCP is automatically enabled to assign an IP address.
- Provide a `dhcp status` command that shows the current IP, subnet mask, gateway, and DNS servers, queried via `esp_netif`.

### 5. Track — Network Security Penetration Tool
- Entering `Track` switches to a sub-environment with the prompt `Track/$`.
- Contains sub-tools:
  - **DEAUTH_tools**: After entering, the prompt becomes `Track/DEAUTH:$`. Typing `help` shows the help list. Supports sending deauth frames by specifying a target AP and client (based on `esp_wifi_80211_tx`).
  - **Zigbee Sniffer**: Based on IEEE 802.15.4 hardware, outputs Zigbee frames over serial or saves them.
  - **Channel Switch**: Quickly switch the current Wi‑Fi channel to assist monitoring (`esp_wifi_set_channel`).

### 6. curl
- Usage matches traditional curl: `curl <url> [-X method] [-H header] [-d data]`.
- Implement using `esp_http_client`, supporting GET, POST, etc., and output the response body and status code.

### 7. Desktop — Simple Desktop Environment
- **Background**: Solid blue.
- **Desktop**: Minimal; by default, only a file manager icon is shown (which opens a file list).
- **Start menu**: Includes power-off and restart options.
- **Shutdown/Restart**: Calls "kernel API" and deep sleep, or displays a power-off prompt (actual control is handled by external circuits; this provides a software shutdown notification).

## ESPAPP Packaging and PKI Integration
- Each tool is compiled as an independent component and ultimately produces a `.espapp` file. The header structure includes: tool name, entry function address, version, and dependency list.
- The PKI tool parses this header and registers the entry function into the main command table, enabling dynamic installation.
- During removal, the tool is removed from the command table and associated resources are released.

Please generate complete C source code 