#include "kernel.h"
#include "wifi.h"

// ============================================================
// Software tool suite — implements prompt_en.MD
// Note: all network/Wi-Fi tools require hardware drivers that
// are unavailable without ESP-IDF ROM blobs. Implementations
// provide full argument parsing + usage framework.
// ============================================================

// === 1. ping — ICMP echo ===

// Simple IPv4 parser: "192.168.5.2" → 0xC0A80502
static uint32_t parse_ipv4(const char* s) {
    uint32_t ip = 0;
    for (int octet = 0; octet < 4; octet++) {
        uint32_t val = 0;
        while (*s >= '0' && *s <= '9') val = val * 10 + (*s++ - '0');
        if (val > 255) return 0;
        ip = (ip << 8) | val;
        if (octet < 3 && *s == '.') s++;
    }
    return ip;
}

void cmd_ping(const char* arg) {
    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("ping: send ICMP ECHO_REQUEST to network hosts\r\n"
                  "Usage: ping <ip> [-c count] [-i interval]\r\n"
                  "Options:\r\n"
                  "  -c <count>    stop after <count> replies\r\n"
                  "  -i <interval> seconds between sending (default 1)\r\n"
                  "Example: ping 192.168.5.2 -c 4\r\n");
        return;
    }

    // Parse arguments
    char target[64]; k_memset(target, 0, sizeof(target));
    int count = -1;
    int interval = 1;
    const char* p = arg;
    char tok[32];

    p = tok_next(p, target, sizeof(target));
    if (!target[0]) { uart_puts("ping: missing host\r\n"); return; }

    while (*p) {
        p = tok_next(p, tok, sizeof(tok));
        if (k_strcmp(tok, "-c") == 0) {
            char cval[16];
            p = tok_next(p, cval, sizeof(cval));
            count = 0;
            for (int i = 0; cval[i] >= '0' && cval[i] <= '9'; i++)
                count = count * 10 + (cval[i] - '0');
        } else if (k_strcmp(tok, "-i") == 0) {
            char ival[16];
            p = tok_next(p, ival, sizeof(ival));
            interval = 0;
            for (int i = 0; ival[i] >= '0' && ival[i] <= '9'; i++)
                interval = interval * 10 + (ival[i] - '0');
            if (interval < 1) interval = 1;
        }
    }

    uint32_t dst_ip = parse_ipv4(target);
    if (dst_ip == 0) {
        // ponytail: no DNS — user must supply IP. Add DNS when UDP stack lands.
        uart_puts("ping: invalid IP address\r\n");
        return;
    }

    // Check network driver. Note: driver_find()->active only means the driver
    // registered OK; the SLIP link is only usable after 'mode network'
    // (net_is_active()). Without that, every net->write() fails silently and
    // the send loop below would never reach its count → infinite "Request
    // timeout". Gate on the real runtime state here.
    driver_t* net = driver_find("network");
    if (!net || !net->active) {
        uart_puts("ping: network driver not registered.\r\n");
        return;
    }
    if (!net_is_active()) {
        uart_puts("ping: SLIP network not active. Run 'mode network' first.\r\n");
        return;
    }

    // Build ICMP echo request
    // IP header (20 bytes) + ICMP header (8 bytes) + payload (56 bytes) = 84 bytes
    uint8_t pkt[84];
    k_memset(pkt, 0, sizeof(pkt));

    // IP header
    pkt[0] = 0x45;  // ver=4, ihl=5
    pkt[2] = (sizeof(pkt) >> 8) & 0xFF;  // total length high
    pkt[3] = sizeof(pkt) & 0xFF;          // total length low
    pkt[4] = 0; pkt[5] = 0;  // id
    pkt[6] = 0; pkt[7] = 0;  // flags/frag
    pkt[8] = 64;  // ttl
    pkt[9] = 1;   // protocol = ICMP
    // checksum at 10-11, filled later
    pkt[12] = 192; pkt[13] = 168; pkt[14] = 5; pkt[15] = 1;  // src=192.168.5.1
    pkt[16] = (dst_ip >> 24) & 0xFF;
    pkt[17] = (dst_ip >> 16) & 0xFF;
    pkt[18] = (dst_ip >> 8) & 0xFF;
    pkt[19] = dst_ip & 0xFF;

    // IP checksum
    uint32_t ip_sum = 0;
    for (int i = 0; i < 10; i++) {  // 20 bytes = 10 half-words
        uint16_t w = (pkt[i*2] << 8) | pkt[i*2 + 1];
        ip_sum += w;
    }
    while (ip_sum >> 16) ip_sum = (ip_sum & 0xFFFF) + (ip_sum >> 16);
    pkt[10] = (~ip_sum >> 8) & 0xFF;
    pkt[11] = ~ip_sum & 0xFF;

    // ICMP header (starts at offset 20)
    pkt[20] = 8;  // type = echo request
    pkt[21] = 0;  // code
    pkt[22] = 0; pkt[23] = 0;  // checksum placeholder
    pkt[24] = 0x12; pkt[25] = 0x34;  // id
    pkt[26] = 0; pkt[27] = 1;  // seq = 1

    // ICMP payload (56 bytes of data)
    const char* payload = "ESP32-C6 SLIP ping from ESPOS kernel";
    int plen = k_strlen(payload);
    for (int i = 0; i < 56 && i < plen; i++)
        pkt[28 + i] = payload[i];

    // ICMP checksum
    int icmp_len = sizeof(pkt) - 20;  // 64 bytes
    uint32_t icmp_sum = 0;
    for (int i = 0; i < icmp_len / 2; i++) {
        uint16_t w = (pkt[20 + i*2] << 8) | pkt[20 + i*2 + 1];
        icmp_sum += w;
    }
    if (icmp_len & 1) icmp_sum += (pkt[20 + icmp_len - 1] << 8);
    while (icmp_sum >> 16) icmp_sum = (icmp_sum & 0xFFFF) + (icmp_sum >> 16);
    pkt[22] = (~icmp_sum >> 8) & 0xFF;
    pkt[23] = ~icmp_sum & 0xFF;

    // Send pings
    int sent = 0, received = 0;
    uint32_t start = uptime_us();
    int max_send = (count > 0) ? count : 999999;
    int ping_seq = 0;  // Fix 7: incrementing seq number

    while (sent < max_send) {
        // Fix 7: update seq in packet before sending
        ping_seq++;
        pkt[26] = (ping_seq >> 8) & 0xFF;
        pkt[27] = ping_seq & 0xFF;
        // Recalculate ICMP checksum (seq changed)
        uint32_t icmp_sum = 0;
        for (int i = 0; i < icmp_len / 2; i++) {
            uint16_t w = (pkt[20 + i*2] << 8) | pkt[20 + i*2 + 1];
            icmp_sum += w;
        }
        if (icmp_len & 1) icmp_sum += (pkt[20 + icmp_len - 1] << 8);
        while (icmp_sum >> 16) icmp_sum = (icmp_sum & 0xFFFF) + (icmp_sum >> 16);
        pkt[22] = (~icmp_sum >> 8) & 0xFF;
        pkt[23] = ~icmp_sum & 0xFF;

        // Send
        int r = net->write(pkt, sizeof(pkt));
        if (r <= 0) {
            // Defensive: never spin forever if the link drops mid-run.
            uart_puts("ping: send failed (network inactive)\r\n");
            break;
        }
        sent++;
        uint32_t send_time = uptime_us();

        // Wait for reply (poll with timeout = 2 * interval seconds)
        uint32_t deadline = send_time + (uint32_t)interval * 2000000;
        // Fix 4: heap-allocated reply buffer (1536 bytes on stack would overflow 2KB task stack)
        uint8_t* reply = (uint8_t*)mm_alloc(1536);
        if (!reply) { uart_puts("ping: out of memory\r\n"); break; }
        int got = 0;

        while (uptime_us() < deadline) {
            net_poll();  // let SLIP process incoming data
            r = net->read(reply, 1536);
            if (r >= (int)sizeof(pkt)) {
                // Check if it's our reply: ICMP echo reply, same id/seq
                uint16_t r_id = (reply[24] << 8) | reply[25];
                uint16_t r_seq = (reply[26] << 8) | reply[27];
                if (reply[20] == 0 && r_id == 0x1234 && r_seq == ping_seq) {
                    uint32_t rtt = (uptime_us() - send_time) / 10;  // centiseconds
                    kprintf("%d bytes from %s: icmp_seq=%d ttl=%d time=%u.%02u ms\r\n",
                            r - 20, target, ping_seq, reply[8], rtt / 100, rtt % 100);
                    received++;
                    got = 1;
                    break;
                }
            }
            task_yield();
        }
        mm_free(reply);

        if (!got) {
            uart_puts("Request timeout\r\n");
        }

        if (sent < max_send) {
            // Wait for interval
            uint32_t next = send_time + (uint32_t)interval * 1000000;
            while (uptime_us() < next) task_yield();
        }
    }

    uint32_t elapsed = (uptime_us() - start) / 1000000;
    if (elapsed == 0) elapsed = 1;
    int loss = (sent > 0) ? ((sent - received) * 100) / sent : 0;  // guard div-by-zero
    kprintf("--- %s ping statistics ---\r\n", target);
    kprintf("%d packets transmitted, %d received, %d%% packet loss, time %u%us\r\n",
            sent, received, loss, elapsed / 60, elapsed % 60);
}

// === 2. wifisearch — scan APs ===

void soft_wifisearch(const char* arg) {
    if (arg[0] && k_strcmp(arg, "-help") == 0) {
        uart_puts("wifisearch: scan for nearby Wi-Fi access points\r\n"
                  "Usage: wifisearch\r\n"
                  "Output: SSID, BSSID (MAC), Channel, RSSI (dBm), Encryption\r\n");
        return;
    }

    if (!wifi_fw_loaded()) {
        uart_puts("Wi-Fi Scan: radio not powered\r\n");
        return;
    }

    struct wifi_ap_t results[MAX_SCAN_RESULTS];
    k_memset(results, 0, sizeof(results));

    int count = wifi_scan(results, MAX_SCAN_RESULTS);
    if (count < 0) {
        uart_puts("Wi-Fi Scan: failed\r\n");
        return;
    }

    uart_puts("Wi-Fi Scan:\r\n");
    uart_puts("  SSID                              BSSID          CH  RSSI    ENC\r\n");
    uart_puts("  ----                              -----          --  ----    ---\r\n");

    for (int i = 0; i < count; i++) {
        // Print SSID
        uart_puts("  ");
        if (results[i].ssid_len > 0) {
            int max_print = results[i].ssid_len;
            if (max_print > 32) max_print = 32;
            for (int j = 0; j < max_print; j++) {
                char c = results[i].ssid[j];
                if (c >= 32 && c < 127)
                    uart_putc(c);
                else
                    uart_putc('.');
            }
            // Pad to 32 chars
            for (int j = max_print; j < 32; j++) uart_putc(' ');
        } else {
            uart_puts("(hidden)                        ");
        }

        // BSSID
        kprintf(" %02x:%02x:%02x:%02x:%02x:%02x",
                results[i].bssid[0], results[i].bssid[1], results[i].bssid[2],
                results[i].bssid[3], results[i].bssid[4], results[i].bssid[5]);

        // Channel
        kprintf(" %2d", results[i].channel);

        // RSSI
        if (results[i].rssi == 0)
            uart_puts("     --");
        else
            kprintf(" %4d", results[i].rssi);

        // Encryption
        switch (results[i].encryption) {
            case WIFI_ENC_OPEN:   uart_puts("  OPEN"); break;
            case WIFI_ENC_WEP:    uart_puts("  WEP"); break;
            case WIFI_ENC_WPA:    uart_puts("  WPA"); break;
            case WIFI_ENC_WPA2:   uart_puts("  WPA2"); break;
            case WIFI_ENC_WPA3:   uart_puts("  WPA3"); break;
            default:              uart_puts("  ???"); break;
        }
        uart_puts("\r\n");
    }

    kprintf("  (%d AP(s) found)\r\n", count);
}

// === 3. wifiinfo — connection details ===

void soft_wifiinfo(const char* arg) {
    if (arg[0] && k_strcmp(arg, "-help") == 0) {
        uart_puts("wifiinfo: show current Wi-Fi connection details\r\n"
                  "Usage: wifiinfo\r\n"
                  "Shows: SSID, MAC, Channel, Signal, Encryption, Rate, IP\r\n");
        return;
    }

    struct wifi_ap_t info;
    int r = wifi_get_ap_info(&info);
    if (r != 0) {
        uart_puts("Wi-Fi Status: ERROR (driver not responding)\r\n");
        return;
    }

    if (wifi_ap_configured()) {
        uart_puts("Wi-Fi Status: AP configured\r\n");
        uart_puts("  Mode:       SoftAP (config)\r\n");
        uart_puts("  SSID:       ");
        for (int i = 0; i < info.ssid_len; i++) uart_putc(info.ssid[i]);
        uart_puts("\r\n");
        uart_puts("  MAC:        ");
        kprintf("%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                info.bssid[0], info.bssid[1], info.bssid[2],
                info.bssid[3], info.bssid[4], info.bssid[5]);
        uart_puts("  Channel:    ");
        kprintf("%d\r\n", info.channel);
        uart_puts("  PHY:        ");
        if (wifi_fw_loaded()) uart_puts("powered (ROM PHY deferred)\r\n");
        else                  uart_puts("not powered\r\n");
        const char* enc = "OPEN";
        if (info.encryption == WIFI_ENC_WEP)  enc = "WEP";
        if (info.encryption == WIFI_ENC_WPA)  enc = "WPA";
        if (info.encryption == WIFI_ENC_WPA2) enc = "WPA2";
        if (info.encryption == WIFI_ENC_WPA3) enc = "WPA3";
        uart_puts("  Encryption: ");
        uart_puts(enc);
        uart_puts("\r\n");
        uart_puts("  Beacon:     ");
        if (wifi_is_active())
            uart_puts("on air\r\n");
        else
            uart_puts("not on air (PHY uncalibrated)\r\n");
        uart_puts("  IP:         (AP mode, no DHCP server yet)\r\n");
        uart_puts("\r\n");
        uart_puts("  Connected stations: (driver not monitoring)\r\n");
    } else {
        uart_puts("Wi-Fi Status: AP not configured\r\n");
        uart_puts("  SSID:       (none)\r\n");
        uart_puts("  MAC:        ");
        kprintf("%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                info.bssid[0], info.bssid[1], info.bssid[2],
                info.bssid[3], info.bssid[4], info.bssid[5]);
        uart_puts("  Channel:    --\r\n");
        uart_puts("  PHY:        ");
        if (wifi_fw_loaded()) uart_puts("powered\r\n");
        else                  uart_puts("not powered\r\n");
        uart_puts("  Signal:     -- dBm\r\n");
        uart_puts("  Encryption: --\r\n");
        uart_puts("  Rate:       --\r\n");
        uart_puts("  IP:         (no address)\r\n");
        uart_puts("Use 'wifi -on' to configure the access point.\r\n");
    }
}

// === 4. DHCP — network config ===

void cmd_dhcp(const char* arg) {
    if (arg[0] && k_strcmp(arg, "-help") == 0) {
        uart_puts("dhcp: DHCP client status and control\r\n"
                  "Usage: dhcp status\r\n"
                  "  status  show current IP, subnet mask, gateway, DNS\r\n");
        return;
    }
    if (!arg[0] || k_strcmp(arg, "status") != 0) {
        uart_puts("Usage: dhcp status\r\n");
        return;
    }

    driver_t* net = driver_find("network");
    int active = net && net->active;

    uart_puts("DHCP Status:\r\n");
    if (active) {
        uart_puts("  Interface:  SLIP over UART0\r\n");
        uart_puts("  IP:         192.168.5.1\r\n");
        uart_puts("  Gateway:    192.168.5.2\r\n");
        uart_puts("  Netmask:    255.255.255.0\r\n");
        uart_puts("  State:      STATIC (SLIP, no DHCP)\r\n");
    } else {
        uart_puts("  State:      DISABLED (network driver not active)\r\n");
        uart_puts("  Use 'mode network' to activate SLIP interface.\r\n");
    }
}

// === 5. Track — sub-environment for network tools ===

static void track_deauth_help() {
    uart_puts("DEAUTH_tools: send 802.11 deauth frames\r\n"
              "Commands:\r\n"
              "  deauth <AP_MAC> [<client_MAC>]  send deauth to AP/client\r\n"
              "  help                             show this help\r\n"
              "  back                             return to Track\r\n"
              "  exit                             return to Track\r\n"
              "Note: requires Wi-Fi monitor mode + raw frame TX\r\n");
}

static void track_deauth_shell() {
    char line[128];
    uart_puts("Entering DEAUTH_tools. Type 'help' for commands, 'back' to exit.\r\n");
    while (1) {
        uart_puts("Track/DEAUTH:$ ");
        console_readline(line, sizeof(line));
        if (k_strcmp(line, "exit") == 0 || k_strcmp(line, "back") == 0 || k_strcmp(line, "q") == 0)
            break;
        if (k_strcmp(line, "help") == 0 || k_strcmp(line, "-help") == 0) {
            track_deauth_help();
            continue;
        }
        if (k_strncmp(line, "deauth", 6) == 0) {
            uart_puts("DEAUTH: sending deauth frames... (driver unavailable)\r\n"
                      "  Requires: esp_wifi_80211_tx() from ESP-IDF\r\n");
            continue;
        }
        kprintf("DEAUTH_tools: unknown command '%s'\r\n", line);
    }
}

static void track_zigbee_help() {
    uart_puts("Zigbee Sniffer: capture IEEE 802.15.4 frames\r\n"
              "Commands:\r\n"
              "  start    start sniffing (output to serial)\r\n"
              "  stop     stop sniffing\r\n"
              "  help     show this help\r\n"
              "  back     return to Track\r\n"
              "Note: requires IEEE 802.15.4 radio init (ESP32-C6 has it)\r\n");
}

static void track_zigbee_shell() {
    char line[128];
    uart_puts("Entering Zigbee Sniffer. Type 'help' for commands, 'back' to exit.\r\n");
    while (1) {
        uart_puts("Track/ZIGBEE:$ ");
        console_readline(line, sizeof(line));
        if (k_strcmp(line, "exit") == 0 || k_strcmp(line, "back") == 0 || k_strcmp(line, "q") == 0)
            break;
        if (k_strcmp(line, "help") == 0 || k_strcmp(line, "-help") == 0) {
            track_zigbee_help();
            continue;
        }
        if (k_strcmp(line, "start") == 0) {
            uart_puts("Zigbee sniffer: starting... (driver unavailable)\r\n");
            continue;
        }
        if (k_strcmp(line, "stop") == 0) {
            uart_puts("Zigbee sniffer: stopped\r\n");
            continue;
        }
        kprintf("Zigbee: unknown command '%s'\r\n", line);
    }
}

static void track_channel_help() {
    uart_puts("Channel Switch: change Wi-Fi monitor channel\r\n"
              "Commands:\r\n"
              "  channel <num>  set channel (1-14)\r\n"
              "  help           show this help\r\n"
              "  back           return to Track\r\n"
              "Note: requires esp_wifi_set_channel() from ESP-IDF\r\n");
}

static void track_channel_shell() {
    char line[128];
    uart_puts("Entering Channel Switch. Type 'help' for commands, 'back' to exit.\r\n");
    while (1) {
        uart_puts("Track/CHANNEL:$ ");
        console_readline(line, sizeof(line));
        if (k_strcmp(line, "exit") == 0 || k_strcmp(line, "back") == 0 || k_strcmp(line, "q") == 0)
            break;
        if (k_strcmp(line, "help") == 0 || k_strcmp(line, "-help") == 0) {
            track_channel_help();
            continue;
        }
        if (k_strncmp(line, "channel", 7) == 0) {
            const char* cp = line + 7;
            while (*cp == ' ') cp++;
            if (*cp) {
                int ch = 0;
                while (*cp >= '0' && *cp <= '9') ch = ch * 10 + (*cp++ - '0');
                if (ch >= 1 && ch <= 14)
                    kprintf("Channel switch: set to %d (driver unavailable)\r\n", ch);
                else
                    uart_puts("Invalid channel. Use 1-14.\r\n");
            }
            continue;
        }
        kprintf("Channel: unknown command '%s'\r\n", line);
    }
}

static void track_help() {
    uart_puts("Track — Network Security Penetration Toolkit\r\n"
              "Sub-tools:\r\n"
              "  DEAUTH          802.11 deauth attack tool\r\n"
              "  ZIGBEE          IEEE 802.15.4 frame sniffer\r\n"
              "  CHANNEL         Wi-Fi channel switch\r\n"
              "  help            show this help\r\n"
              "  exit/back       return to shell\r\n"
              "Enter a tool name to enter its sub-environment.\r\n");
}

void cmd_track(const char* arg) {
    if (arg[0] && k_strcmp(arg, "-help") == 0) {
        track_help();
        return;
    }
    char line[128];
    uart_puts("Track — Network Security Toolkit. Type 'help'.\r\n");
    while (1) {
        uart_puts("Track/$ ");
        console_readline(line, sizeof(line));
        if (k_strcmp(line, "exit") == 0 || k_strcmp(line, "back") == 0 || k_strcmp(line, "quit") == 0)
            break;
        if (k_strcmp(line, "help") == 0 || k_strcmp(line, "-help") == 0) {
            track_help();
            continue;
        }
        if (k_strcmp(line, "deauth") == 0 || k_strcmp(line, "DEAUTH") == 0) {
            track_deauth_shell();
            continue;
        }
        if (k_strcmp(line, "zigbee") == 0 || k_strcmp(line, "ZIGBEE") == 0) {
            track_zigbee_shell();
            continue;
        }
        if (k_strcmp(line, "channel") == 0 || k_strcmp(line, "CHANNEL") == 0) {
            track_channel_shell();
            continue;
        }
        kprintf("Track: unknown command '%s'. Type 'help'.\r\n", line);
    }
}

// === 6. curl — HTTP client ===

void cmd_curl(const char* arg) {
    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("curl: transfer data from/to network servers\r\n"
                  "Usage: curl <url> [-X method] [-H header] [-d data]\r\n"
                  "Options:\r\n"
                  "  -X <method>    HTTP method (GET, POST, etc.)\r\n"
                  "  -H <header>    custom header\r\n"
                  "  -d <data>      request body\r\n"
                  "Example: curl http://example.com -X GET\r\n");
        return;
    }

    char url[128];    k_memset(url, 0, sizeof(url));
    char method[8];   k_memset(method, 0, sizeof(method));
    char header[64];  k_memset(header, 0, sizeof(header));
    char data[64];    k_memset(data, 0, sizeof(data));
    const char* p = arg;

    // URL is first token
    p = tok_next(p, url, sizeof(url));
    if (!url[0]) { uart_puts("curl: missing URL\r\n"); return; }
    if (k_strncmp(url, "http://", 7) != 0 && k_strncmp(url, "https://", 8) != 0) {
        uart_puts("curl: only HTTP(S) URLs supported\r\n");
        return;
    }

    k_strcpy(method, "GET");
    char tok[32];
    while (*p) {
        p = tok_next(p, tok, sizeof(tok));
        if (k_strcmp(tok, "-X") == 0) {
            p = tok_next(p, method, sizeof(method));
        } else if (k_strcmp(tok, "-H") == 0) {
            p = tok_next(p, header, sizeof(header));
        } else if (k_strcmp(tok, "-d") == 0) {
            p = tok_next(p, data, sizeof(data));
        }
    }

    kprintf("curl: %s %s\r\n", method, url);
    if (header[0]) kprintf("  Header: %s\r\n", header);
    if (data[0])  kprintf("  Body:   %s\r\n", data);
    driver_t* net = driver_find("network");
    if (net && net->active)
        uart_puts("  -> HTTP client: SLIP active, TCP stack not yet implemented\r\n");
    else
        uart_puts("  -> HTTP client: network not active (use 'mode network')\r\n");
}

// === 7. Desktop — ANSI terminal GUI ===
//
// The desktop is drawn and driven entirely here on the ESP and streamed as
// ANSI to the terminal / the monitor's Cairo canvas. The ESP is the single
// source of truth: it owns all layout and logic, so the desktop works over any
// serial terminal by keyboard. The monitor additionally translates canvas
// mouse events into a custom CSI (see desktop_read_event) so it is clickable.
//
// IMPORTANT: everything drawn must be single-byte ASCII — the canvas maps one
// byte to one cell, so UTF-8 box-drawing chars garble. Use +, -, | only.

// ANSI escape sequences / colour attributes
#define ESC "\x1b"
#define CSI ESC "["
#define CLR CSI "2J"
#define HOME CSI "H"
#define BLUE_BG  CSI "44;37;1m"   // workspace: blue bg, bright white fg
#define WHITE_BG CSI "47;30m"     // window/menu body: white bg, black fg
#define MENUBAR  CSI "47;30m"     // top menu bar: white bg, black fg
#define TASKBAR  CSI "44;33;1m"   // taskbar: blue bg, yellow fg
#define TITLE    CSI "100;97;1m"  // window title bar: gray bg, bright white fg
#define RESET    CSI "0m"
#define MENU_HL  CSI "46;30;1m"   // highlight: cyan bg, black fg
#define CLOSEBTN CSI "101;97;1m"  // window close button: bright-red bg, white fg

#define DESK_COLS 80
#define DESK_ROWS 24

static void desktop_at(int row, int col) { kprintf(CSI "%d;%dH", row, col); }
static void put_spaces(int n) { for (int i = 0; i < n; i++) uart_putc(' '); }
static void put_pad(const char* s, int w) {
    int n = 0;
    for (; *s && n < w; s++, n++) uart_putc(*s);
    for (; n < w; n++) uart_putc(' ');
}
static void put_center(const char* s, int w) {
    int len = k_strlen(s); if (len > w) len = w;
    int left = (w - len) / 2;
    put_spaces(left);
    for (int i = 0; i < len; i++) uart_putc(s[i]);
    put_spaces(w - left - len);
}

// --- Clickable hot-regions --------------------------------------------------
// Rebuilt on every full redraw. A mouse click/hover looks up the region under
// the cell and maps it to an action, shared with keyboard dispatch.
enum {
    ACT_NONE = 0,
    ACT_ICON_FILES, ACT_ICON_TERM, ACT_ICON_SET,
    ACT_ICON_PKI,   ACT_ICON_NET,  ACT_ICON_ABOUT,
    ACT_MENU_SYS, ACT_MENU_APPS, ACT_START,
    ACT_SYS_ABOUT, ACT_SYS_NET, ACT_SYS_RESTART, ACT_SYS_SHUTDOWN,
    ACT_PKI_REMOVE,
    ACT_CLOSE,          // window [X] close button (all modal windows)
};
#define ACT_PKI_ROW0 200   // package-list rows: action = ACT_PKI_ROW0 + index
#define ACT_FM_ROW0  400   // file-manager rows: action = ACT_FM_ROW0 + display index

struct HotRegion { int r0, r1, c0, c1, action; };
static HotRegion g_regions[64];
static int g_region_count;
static void region_reset() { g_region_count = 0; }
static void region_add(int r0, int r1, int c0, int c1, int action) {
    if (g_region_count < (int)(sizeof(g_regions) / sizeof(g_regions[0])))
        g_regions[g_region_count++] = { r0, r1, c0, c1, action };
}
static int region_hit(int row, int col) {
    // Last-registered wins so dropdowns drawn on top of icons take priority.
    for (int i = g_region_count - 1; i >= 0; i--) {
        HotRegion& h = g_regions[i];
        if (row >= h.r0 && row <= h.r1 && col >= h.c0 && col <= h.c1)
            return h.action;
    }
    return ACT_NONE;
}

// --- Icons ------------------------------------------------------------------
// Icons are laid out on a uniform, top-left-anchored grid (like a real desktop):
// position is COMPUTED from the index, so the arrangement stays tidy and adding
// or removing an icon needs no manual repositioning. DESK_ICONS carries only the
// appearance + action.
struct DeskIcon { const char* glyph; const char* label; int bg; int action; };
static const DeskIcon DESK_ICONS[6] = {
    { "[F]", "Files",    41,  ACT_ICON_FILES },  // red
    { "[>]", "Terminal", 42,  ACT_ICON_TERM  },  // green
    { "[*]", "Settings", 45,  ACT_ICON_SET   },  // magenta
    { "[P]", "PKI",      43,  ACT_ICON_PKI   },  // yellow
    { "[N]", "Network",  100, ACT_ICON_NET   },  // gray  (avoid selection cyan)
    { "[i]", "About",    104, ACT_ICON_ABOUT },  // bright blue (avoid workspace blue)
};
#define ICON_COUNT  6
#define GRID_COLS   3
#define TILE_W      12
#define ICON_GAP_X  6         // horizontal gutter between tiles
#define ICON_GAP_Y  1         // vertical gutter between rows
#define ICON_ORG_R  4         // grid origin (top-left) row
#define ICON_ORG_C  6         // grid origin column
// A tile is 3 rows tall + 1 label row = 4 rows.
static int icon_col(int i) { return ICON_ORG_C + (i % GRID_COLS) * (TILE_W + ICON_GAP_X); }
static int icon_row(int i) { return ICON_ORG_R + (i / GRID_COLS) * (4 + ICON_GAP_Y); }

// A graphical icon = a 3-row colour tile (app colour, or cyan when selected)
// with a centred glyph, plus a label row below. Registers its hot region.
static void desktop_draw_icon(int i, int selected) {
    const DeskIcon& ic = DESK_ICONS[i];
    int row = icon_row(i), col = icon_col(i);
    for (int k = 0; k < 3; k++) {
        desktop_at(row + k, col);
        if (selected) uart_puts(MENU_HL);
        else          kprintf(CSI "%d;97;1m", ic.bg);   // colour bg, bright white fg
        if (k == 1) put_center(ic.glyph, TILE_W);
        else        put_spaces(TILE_W);
        uart_puts(RESET);
    }
    desktop_at(row + 3, col);
    uart_puts(selected ? MENU_HL : BLUE_BG);
    put_center(ic.label, TILE_W);
    uart_puts(RESET);
    region_add(row, row + 3, col, col + TILE_W - 1, ic.action);
}

// --- Dropdown menus ---------------------------------------------------------
struct MenuDef { int n; const char* items[4]; int actions[4]; };
static const MenuDef MENU_SYSTEM = { 4,
    { "About", "Network", "Restart", "Shutdown" },
    { ACT_SYS_ABOUT, ACT_SYS_NET, ACT_SYS_RESTART, ACT_SYS_SHUTDOWN } };
static const MenuDef MENU_APPS = { 4,
    { "Files", "Terminal", "PKI", "Settings" },
    { ACT_ICON_FILES, ACT_ICON_TERM, ACT_ICON_PKI, ACT_ICON_SET } };

#define DD_W 16
static void desktop_dropdown(int top, int col, const MenuDef* m, int sel) {
    desktop_at(top, col);
    uart_puts(WHITE_BG "+"); for (int i = 0; i < DD_W; i++) uart_putc('-'); uart_puts("+" RESET);
    for (int i = 0; i < m->n; i++) {
        desktop_at(top + 1 + i, col);
        uart_puts(WHITE_BG "|");
        uart_puts(i == sel ? MENU_HL : WHITE_BG);
        uart_putc(i == sel ? '>' : ' '); uart_putc(' ');
        put_pad(m->items[i], DD_W - 2);
        uart_puts(WHITE_BG "|" RESET);
        region_add(top + 1 + i, top + 1 + i, col, col + DD_W + 1, m->actions[i]);
    }
    desktop_at(top + 1 + m->n, col);
    uart_puts(WHITE_BG "+"); for (int i = 0; i < DD_W; i++) uart_putc('-'); uart_puts("+" RESET);
}

// --- Modal windows ----------------------------------------------------------
// Shared window chrome: an ASCII-bordered frame with a coloured title bar and a
// clickable [X] close button at the top-right. Body area is the white interior
// at rows top+2 .. top+h-2, columns col+2 .. col+W-3. The [X] registers an
// ACT_CLOSE hot-region so every window closes the same way (button, Esc, or
// click-outside). Keeping one renderer makes all windows look consistent.
static void desktop_window_frame(int top, int col, int W, int h, const char* title) {
    for (int r = top; r < top + h; r++) {
        desktop_at(r, col);
        uart_puts(WHITE_BG);
        if (r == top || r == top + h - 1) {          // top / bottom border
            uart_putc('+');
            for (int i = 0; i < W - 2; i++) uart_putc('-');
            uart_putc('+');
        } else {                                     // side borders + white body
            uart_putc('|');
            put_spaces(W - 2);
            uart_putc('|');
        }
        uart_puts(RESET);
    }
    // Title bar spans the interior of row top+1, title left, [X] button right.
    desktop_at(top + 1, col + 1);
    uart_puts(TITLE);
    uart_putc(' ');
    put_pad(title, W - 6);                            // leaves 3 cells for [X]
    uart_puts(RESET);
    desktop_at(top + 1, col + W - 4);
    uart_puts(CLOSEBTN "[X]" RESET);
    region_add(top + 1, top + 1, col + W - 4, col + W - 2, ACT_CLOSE);
}

static void desktop_modal(const char* title, const char* const* lines, int n) {
    const int W = 44;
    const int col = (DESK_COLS - W) / 2 + 1;
    const int top = 6;
    const int h = n + 4;
    desktop_window_frame(top, col, W, h, title);
    for (int i = 0; i < n; i++) {
        desktop_at(top + 2 + i, col + 2);
        uart_puts(WHITE_BG); put_pad(lines[i], W - 4); uart_puts(RESET);
    }
    desktop_at(top + h - 2, col + 2);
    uart_puts(WHITE_BG); put_pad("[X] button, [Esc], or click to close", W - 4); uart_puts(RESET);
}

// --- File Manager (interactive VFS browser, modal == 1) ---------------------
// Browses the real in-memory filesystem: lists the current directory, navigate
// with arrows, Enter/click a [DIR] to descend, "[..]" (or Backspace) to go up.
// vfs_list's callback carries no context pointer, so results accumulate into
// file-static arrays (same pattern as the PKI scan).
#define FM_MAX  32          // entries captured per directory
#define FM_VIS  12          // rows shown at once (window height cap)
static char g_fm_path[VFS_MAX_PATH];
static char g_fm_names[FM_MAX][VFS_MAX_NAME];
static char g_fm_types[FM_MAX];
static int  g_fm_sizes[FM_MAX];
static int  g_fm_count;

static void fm_scan_cb(const char* name, int type, int size) {
    if (g_fm_count < FM_MAX) {
        k_strncpy(g_fm_names[g_fm_count], name, VFS_MAX_NAME - 1);
        g_fm_names[g_fm_count][VFS_MAX_NAME - 1] = '\0';
        g_fm_types[g_fm_count] = (char)type;
        g_fm_sizes[g_fm_count] = size;
        g_fm_count++;
    }
}
static void desktop_fm_scan() { g_fm_count = 0; vfs_list(g_fm_path, fm_scan_cb); }
static void desktop_fm_open() { k_strcpy(g_fm_path, "/"); desktop_fm_scan(); }
static int  fm_at_root() { return g_fm_path[0] == '/' && g_fm_path[1] == '\0'; }

// Append a child directory name to the current path.
static void fm_enter(const char* name) {
    int len = k_strlen(g_fm_path);
    if (!(len == 1 && g_fm_path[0] == '/')) {        // add '/' unless at root
        if (len < VFS_MAX_PATH - 1) { g_fm_path[len++] = '/'; g_fm_path[len] = '\0'; }
    }
    k_strncpy(g_fm_path + len, name, VFS_MAX_PATH - 1 - len);
    g_fm_path[VFS_MAX_PATH - 1] = '\0';
}
// Drop the last path component ("/a/b" -> "/a", "/a" -> "/").
static void fm_up() {
    int len = k_strlen(g_fm_path);
    while (len > 1 && g_fm_path[len - 1] == '/') g_fm_path[--len] = '\0';
    while (len > 1 && g_fm_path[len - 1] != '/') g_fm_path[--len] = '\0';
    if (len > 1 && g_fm_path[len - 1] == '/') g_fm_path[--len] = '\0';
}

// Rows shown = optional "[..]" up-entry, then the directory entries (capped).
static int fm_has_up()   { return !fm_at_root(); }
static int fm_row_total(){ int t = g_fm_count + (fm_has_up() ? 1 : 0);
                           return t > FM_VIS ? FM_VIS : t; }

// Activate the selected row. Returns 1 if the listing changed (needs redraw).
static int desktop_fm_activate(int* sel) {
    int up = fm_has_up();
    if (up && *sel == 0) { fm_up(); desktop_fm_scan(); *sel = 0; return 1; }
    int idx = *sel - (up ? 1 : 0);
    if (idx < 0 || idx >= g_fm_count) return 0;
    if (g_fm_types[idx] == VFS_TYPE_DIR) {
        fm_enter(g_fm_names[idx]); desktop_fm_scan(); *sel = 0; return 1;
    }
    return 0;   // files: no navigation (details shown live in the status line)
}

static void desktop_files_window(int sel) {
    const int W = 50;
    const int col = (DESK_COLS - W) / 2 + 1;
    const int top = 3;
    int up = fm_has_up();
    int rows = fm_row_total();
    int listrows = rows > 0 ? rows : 1;
    const int h = listrows + 5;   // border,title,path,list,status,border
    desktop_window_frame(top, col, W, h, "File Manager");

    desktop_at(top + 2, col + 2); uart_puts(WHITE_BG);
    kprintf("Path: "); put_pad(g_fm_path, W - 4 - 6); uart_puts(RESET);

    for (int i = 0; i < listrows; i++) {
        int r = top + 3 + i;
        int isSel = (i == sel);
        desktop_at(r, col + 2);
        uart_puts(isSel ? MENU_HL : WHITE_BG);
        uart_putc(isSel ? '>' : ' '); uart_putc(' ');
        if (rows == 0) {
            put_pad("(empty directory)", W - 6);
        } else if (up && i == 0) {
            put_pad("[..]  up one level", W - 6);
        } else {
            int idx = i - (up ? 1 : 0);
            char rowbuf[VFS_MAX_NAME + 8];
            k_strcpy(rowbuf, g_fm_types[idx] == VFS_TYPE_DIR ? "[DIR] " : "[FIL] ");
            k_strncpy(rowbuf + 6, g_fm_names[idx], VFS_MAX_NAME - 1);
            rowbuf[VFS_MAX_NAME + 6] = '\0';
            put_pad(rowbuf, W - 6);
        }
        uart_puts(RESET);
        region_add(r, r, col, col + W - 1, ACT_FM_ROW0 + i);
    }

    // Status line reflects the current selection.
    desktop_at(top + h - 2, col + 2); uart_puts(WHITE_BG);
    char st[VFS_MAX_NAME + 24];
    if (rows == 0) {
        k_strcpy(st, "Enter: none   [X]/Esc: close");
    } else if (up && sel == 0) {
        k_strcpy(st, "Enter: go up   [X]/Esc: close");
    } else {
        int idx = sel - (up ? 1 : 0);
        if (idx >= 0 && idx < g_fm_count && g_fm_types[idx] == VFS_TYPE_FILE) {
            char sizebuf[12];   // decimal size, built by hand (no snprintf in kernel)
            int n = 0, v = g_fm_sizes[idx];
            if (v == 0) sizebuf[n++] = '0';
            while (v > 0 && n < 11) { sizebuf[n++] = '0' + (v % 10); v /= 10; }
            sizebuf[n] = '\0';
            for (int a = 0, b = n - 1; a < b; a++, b--) { char t = sizebuf[a]; sizebuf[a] = sizebuf[b]; sizebuf[b] = t; }
            k_strcpy(st, "FILE "); k_strncpy(st + 5, g_fm_names[idx], sizeof(st) - 20);
            st[sizeof(st) - 20] = '\0';
            int len = k_strlen(st);
            k_strcpy(st + len, " ("); k_strncpy(st + len + 2, sizebuf, 11);
            k_strcpy(st + len + 2 + k_strlen(sizebuf), " bytes)");
        } else {
            k_strcpy(st, "DIR   "); k_strncpy(st + 6, g_fm_names[idx], sizeof(st) - 7);
        }
    }
    put_pad(st, W - 4);
    uart_puts(RESET);
}
static void desktop_show_about() {
    static const char* const lines[] = {
        "ESPOS Kernel v0.3", "ESP32-C6 @ 40MHz",
        "RV32IMAC, 512KB SRAM", "(c) 2026 ESPOS",
    };
    desktop_modal("About ESPOS", lines, 4);
}
static void desktop_show_settings() {
    static const char* const lines[] = {
        "Display : ANSI 80 x 24", "Mouse   : enabled",
        "No configurable settings yet.",
    };
    desktop_modal("Settings", lines, 3);
}
static char* hex2(char* p, unsigned v) {
    const char* H = "0123456789abcdef";
    *p++ = H[(v >> 4) & 0xf]; *p++ = H[v & 0xf]; return p;
}
static void desktop_show_network() {
    char macline[40];
    char* p = macline;
    const char* pre = "MAC:   ";
    while (*pre) *p++ = *pre++;
    for (int i = 0; i < 6; i++) { p = hex2(p, g_wifi_mac[i]); if (i < 5) *p++ = ':'; }
    *p = 0;
    const char* lines[3] = { "Wi-Fi: unavailable", "IP:    none", macline };
    desktop_modal("Network Status", lines, 3);
}

// --- Package Manager window (interactive, modal == 5) -----------------------
// Lists packages installed under /app (each PKI package is a directory there),
// lets the user select one and Remove it (root-gated). Install stays a shell op.
#define PKI_MAX 16
static char g_pki_names[PKI_MAX][VFS_MAX_NAME];
static int  g_pki_count;
static char g_pki_msg[40];   // status line (last action result)

static void pki_scan_cb(const char* name, int type, int size) {
    (void)size;
    if (type == VFS_TYPE_DIR && g_pki_count < PKI_MAX)
        k_strncpy(g_pki_names[g_pki_count++], name, VFS_MAX_NAME - 1);
}
static void desktop_pki_scan() {
    g_pki_count = 0;
    vfs_list("/app", pki_scan_cb);   // no-op if /app is absent
}

// Remove the selected package (root only). Updates g_pki_msg + rescans.
static void desktop_pki_remove(int sel) {
    if (g_pki_count == 0) { k_strcpy(g_pki_msg, "No packages to remove"); return; }
    if (!user_check_root()) { k_strcpy(g_pki_msg, "Remove needs root (su)"); return; }
    char path[VFS_MAX_PATH];
    k_strcpy(path, "/app/");
    k_strncpy(path + 5, g_pki_names[sel], sizeof(path) - 6);
    int r = vfs_delete(path);
    if (r == E_OK) { k_strcpy(g_pki_msg, "Removed "); k_strncpy(g_pki_msg + 8, g_pki_names[sel], 30); }
    else           k_strcpy(g_pki_msg, "Remove failed");
    desktop_pki_scan();
}

static void desktop_pki_window(int sel) {
    const int W = 52;
    const int col = (DESK_COLS - W) / 2 + 1;
    const int top = 4;
    const int listrows = g_pki_count > 0 ? g_pki_count : 1;
    const int h = listrows + 7;   // border,title,subtitle,list,blank,button,msg,border
    desktop_window_frame(top, col, W, h, "Package Manager");

    desktop_at(top + 2, col + 2); uart_puts(WHITE_BG);
    kprintf("Installed in /app:  %d", g_pki_count);
    uart_puts(RESET);

    for (int i = 0; i < listrows; i++) {
        int r = top + 3 + i;
        desktop_at(r, col + 2);
        if (g_pki_count == 0) {
            uart_puts(WHITE_BG); put_pad("(none installed - use: pki -i <f>.espapp)", W - 4); uart_puts(RESET);
        } else {
            uart_puts(i == sel ? MENU_HL : WHITE_BG);
            uart_putc(i == sel ? '>' : ' '); uart_putc(' ');
            char row[VFS_MAX_NAME + 8];
            k_strcpy(row, "[PKG] ");
            k_strncpy(row + 6, g_pki_names[i], VFS_MAX_NAME - 1);
            put_pad(row, W - 6);
            uart_puts(RESET);
            region_add(r, r, col, col + W - 1, ACT_PKI_ROW0 + i);
        }
    }
    int br = top + 3 + listrows + 1;   // button row (after a blank line)
    desktop_at(br, col + 2);
    uart_puts(MENU_HL " [ Remove ] " RESET);
    region_add(br, br, col + 2, col + 13, ACT_PKI_REMOVE);
    desktop_at(br, col + 16);
    uart_puts(WHITE_BG); put_pad("r: remove   arrows: select", W - 18); uart_puts(RESET);
    desktop_at(top + h - 2, col + 2);
    uart_puts(WHITE_BG); put_pad(g_pki_msg, W - 4); uart_puts(RESET);
}

// --- Full redraw (rebuilds hot-regions) -------------------------------------
static void desktop_base(int sel, int open_menu, int modal) {
    region_reset();
    uart_puts(CLR HOME);
    // Menu bar (row 1): white bar with clickable titles + a right-aligned clock.
    desktop_at(1, 1); uart_puts(MENUBAR); put_spaces(DESK_COLS);
    desktop_at(1, 1);
    uart_puts((open_menu == 1 || open_menu == 3) ? MENU_HL : MENUBAR); uart_puts(" System ");
    uart_puts(open_menu == 2 ? MENU_HL : MENUBAR); uart_puts(" Apps ");
    desktop_at(1, DESK_COLS - 7); uart_puts(MENUBAR "12:00  " RESET);
    region_add(1, 1, 1, 8, ACT_MENU_SYS);    // " System "
    region_add(1, 1, 9, 14, ACT_MENU_APPS);  // " Apps "
    // Workspace fill (rows 2..23, includes the hint row so it has no black gaps)
    for (int r = 2; r <= 23; r++) {
        desktop_at(r, 1); uart_puts(BLUE_BG); put_spaces(DESK_COLS); uart_puts(RESET);
    }
    if (!modal) {
        for (int i = 0; i < 6; i++) desktop_draw_icon(i, i == sel && !open_menu);
        desktop_at(23, 3);
        uart_puts(BLUE_BG "Mouse or arrows to move   Enter/click: open   ?: menu   Esc: exit" RESET);
    }
    // Taskbar (row 24)
    desktop_at(24, 1);
    uart_puts(open_menu == 3 ? MENU_HL : TASKBAR); uart_puts(" [Start] ");
    uart_puts(TASKBAR);
    put_pad(" ESPOS Desktop v0.3", DESK_COLS - 9 - 7);
    uart_puts("12:00  " RESET);
    region_add(24, 24, 1, 9, ACT_START);     // " [Start] "
}

static void desktop_redraw_full(int sel, int open_menu, int menu_sel, int modal, int pki_sel, int fm_sel) {
    desktop_base(sel, open_menu, modal);
    if (open_menu == 1)      desktop_dropdown(2, 1, &MENU_SYSTEM, menu_sel);
    else if (open_menu == 2) desktop_dropdown(2, 9, &MENU_APPS, menu_sel);
    else if (open_menu == 3) desktop_dropdown(24 - (MENU_SYSTEM.n + 2), 1, &MENU_SYSTEM, menu_sel);
    if (modal == 1)      desktop_files_window(fm_sel);
    else if (modal == 2) desktop_show_about();
    else if (modal == 3) desktop_show_network();
    else if (modal == 4) desktop_show_settings();
    else if (modal == 5) desktop_pki_window(pki_sel);
}

// --- Action dispatch (shared by keyboard and mouse) -------------------------
// Returns: 0 continue, 1 exit desktop (return), 2 shutdown (break).
static int desktop_dispatch(int action, int* open_menu, int* menu_sel, int* modal, int* fm_sel) {
    *open_menu = 0; *menu_sel = 0;
    switch (action) {
        case ACT_ICON_FILES:
            desktop_fm_open(); *fm_sel = 0; *modal = 1; break;
        case ACT_ICON_TERM:
            uart_puts(CLR HOME RESET "Terminal launched.\r\n"); return 1;
        case ACT_ICON_SET:                      *modal = 4; break;
        case ACT_ICON_PKI:
            desktop_pki_scan(); g_pki_msg[0] = '\0'; *modal = 5; break;
        case ACT_ICON_NET:  case ACT_SYS_NET:   *modal = 3; break;
        case ACT_ICON_ABOUT: case ACT_SYS_ABOUT: *modal = 2; break;
        case ACT_CLOSE:                         *modal = 0; break;   // window [X]
        case ACT_SYS_RESTART: system_reset(); break;   // does not return
        case ACT_SYS_SHUTDOWN: {
            uart_puts(CLR HOME);
            mailbox_t* mb = (mailbox_t*)MAILBOX_ADDR;
            mb->magic = MB_MAGIC_VALUE;
            mb->status = KERN_STATUS_REBOOT;
            mb->request = BOOT_MODE_NORMAL;
            mb->retry = 0;
            uart_puts("Shutting down... System halted.\r\n");
            return 2;
        }
        default: break;
    }
    return 0;
}

// --- Input event reader -----------------------------------------------------
enum { EV_KEY = 0, EV_ARROW = 1, EV_MOUSE = 2 };
struct DeskEvent { int type; int ch; int row; int col; };  // mouse: ch=0 hover,1 click

static int desktop_getc_to(uint32_t us) {
    uint32_t t0 = get_mcycle();
    while (!uart_avail())
        if ((get_mcycle() - t0) > us * CYCLES_PER_US) return -1;
    return uart_getc();
}

// Reads one desktop event. Distinguishes a lone ESC from an arrow (ESC [ A..D)
// from a mouse event (custom CSI: ESC [ <e> ; <row> ; <col> M, e=0 hover/1 click).
static DeskEvent desktop_read_event() {
    DeskEvent e = { EV_KEY, 0, 0, 0 };
    int c = uart_getc();
    if (c != 0x1B) { e.ch = c; return e; }

    int b1 = desktop_getc_to(5000);
    if (b1 != '[' && b1 != 'O') { e.ch = 0x1B; return e; }   // lone ESC (or timeout)
    int b2 = desktop_getc_to(5000);
    if (b2 < 0) { e.ch = 0x1B; return e; }

    if (b2 >= '0' && b2 <= '9') {                             // mouse event
        int nums[3] = { 0, 0, 0 }, ni = 0, cur = b2 - '0';
        for (int guard = 0; guard < 24; guard++) {
            int b = desktop_getc_to(5000);
            if (b < 0) break;
            if (b >= '0' && b <= '9') cur = cur * 10 + (b - '0');
            else if (b == ';') { if (ni < 3) nums[ni++] = cur; cur = 0; }
            else if (b == 'M') { if (ni < 3) nums[ni++] = cur; break; }
            else break;
        }
        e.type = EV_MOUSE; e.ch = nums[0]; e.row = nums[1]; e.col = nums[2];
        return e;
    }
    e.type = EV_ARROW; e.ch = b2; return e;                  // arrow / function key
}

void cmd_desktop(const char* arg) {
    if (arg[0] && k_strcmp(arg, "-help") == 0) {
        uart_puts("desktop: graphical desktop environment\r\n"
                  "Usage: desktop\r\n"
                  "Menu bar (System/Apps), icon grid, Start menu.\r\n"
                  "Controls: mouse click/hover, arrows/WASD, Enter open, ? menu, Esc exit\r\n");
        return;
    }

    int sel = 0, open_menu = 0, menu_sel = 0, modal = 0, pki_sel = 0, fm_sel = 0;
    desktop_redraw_full(sel, open_menu, menu_sel, modal, pki_sel, fm_sel);

    while (1) {
        DeskEvent ev = desktop_read_event();
        int prev_sel = sel, prev_menu = open_menu, prev_msel = menu_sel,
            prev_modal = modal, prev_pki = pki_sel, prev_fm = fm_sel;
        int quit = 0, force = 0;

        if (modal == 1) {
            // File Manager window: navigate directories with arrows, Enter/click
            // to descend (or go up on "[..]"), Backspace / "u" goes up.
            if (ev.type == EV_ARROW) {
                if (ev.ch == 'A' && fm_sel > 0) fm_sel--;
                else if (ev.ch == 'B' && fm_sel < fm_row_total() - 1) fm_sel++;
            } else if (ev.type == EV_KEY) {
                if (ev.ch == 0x1B || ev.ch == 'q' || ev.ch == 'x') modal = 0;
                else if ((ev.ch == 'w' || ev.ch == 'W') && fm_sel > 0) fm_sel--;
                else if ((ev.ch == 's' || ev.ch == 'S') && fm_sel < fm_row_total() - 1) fm_sel++;
                else if (ev.ch == 0x0D || ev.ch == 'e' || ev.ch == 'E')
                    force = desktop_fm_activate(&fm_sel);
                else if (ev.ch == 0x08 || ev.ch == 0x7F || ev.ch == 'u' || ev.ch == 'U')
                    { fm_up(); desktop_fm_scan(); fm_sel = 0; force = 1; }   // up one level
            } else if (ev.type == EV_MOUSE) {
                int a = region_hit(ev.row, ev.col);
                if (a >= ACT_FM_ROW0) {          // hover or click a listing row
                    fm_sel = a - ACT_FM_ROW0;
                    if (ev.ch == 1) force = desktop_fm_activate(&fm_sel);
                } else if (a == ACT_CLOSE) modal = 0;          // window [X]
                else if (ev.ch == 1) modal = 0;                // click outside closes
            }
        } else if (modal == 5) {
            // Package Manager window: select a package, Remove it, or close.
            if (ev.type == EV_ARROW) {
                if (ev.ch == 'A' && pki_sel > 0) pki_sel--;
                else if (ev.ch == 'B' && pki_sel < g_pki_count - 1) pki_sel++;
            } else if (ev.type == EV_KEY) {
                if (ev.ch == 0x1B || ev.ch == 'q' || ev.ch == 'x') modal = 0;
                else if ((ev.ch == 'w' || ev.ch == 'W') && pki_sel > 0) pki_sel--;
                else if ((ev.ch == 's' || ev.ch == 'S') && pki_sel < g_pki_count - 1) pki_sel++;
                else if (ev.ch == 'r' || ev.ch == 'R') { desktop_pki_remove(pki_sel);
                    if (pki_sel >= g_pki_count) pki_sel = g_pki_count > 0 ? g_pki_count - 1 : 0;
                    force = 1; }
            } else if (ev.type == EV_MOUSE) {
                int a = region_hit(ev.row, ev.col);
                if (a >= ACT_PKI_ROW0) pki_sel = a - ACT_PKI_ROW0;   // hover or click a row
                else if (ev.ch == 1) {
                    if (a == ACT_PKI_REMOVE) { desktop_pki_remove(pki_sel);
                        if (pki_sel >= g_pki_count) pki_sel = g_pki_count > 0 ? g_pki_count - 1 : 0;
                        force = 1; }
                    else if (a == ACT_CLOSE) modal = 0;   // window [X]
                    else modal = 0;                       // click outside closes
                }
            }
        } else if (modal) {
            // Static modal window: any click, Esc, q or x closes it.
            if (ev.type == EV_KEY && (ev.ch == 0x1B || ev.ch == 'q' || ev.ch == 'x')) modal = 0;
            else if (ev.type == EV_MOUSE) {
                int a = region_hit(ev.row, ev.col);
                if (a == ACT_CLOSE || ev.ch == 1) modal = 0;   // [X] or any click
            }
        } else if (open_menu) {
            const MenuDef* m = (open_menu == 2) ? &MENU_APPS : &MENU_SYSTEM;
            if (ev.type == EV_ARROW) {
                if (ev.ch == 'A' && menu_sel > 0) menu_sel--;
                else if (ev.ch == 'B' && menu_sel < m->n - 1) menu_sel++;
            } else if (ev.type == EV_KEY) {
                if ((ev.ch == 'w' || ev.ch == 'W') && menu_sel > 0) menu_sel--;
                else if ((ev.ch == 's' || ev.ch == 'S') && menu_sel < m->n - 1) menu_sel++;
                else if (ev.ch == 0x0D || ev.ch == 'e' || ev.ch == 'E')
                    quit = desktop_dispatch(m->actions[menu_sel], &open_menu, &menu_sel, &modal, &fm_sel);
                else if (ev.ch == 0x1B || ev.ch == 'q') open_menu = 0;
            } else if (ev.type == EV_MOUSE) {
                int a = region_hit(ev.row, ev.col);
                int idx = -1;
                for (int i = 0; i < m->n; i++) if (m->actions[i] == a) { idx = i; break; }
                if (ev.ch == 0) {                            // hover highlights item
                    if (idx >= 0) menu_sel = idx;
                } else if (idx >= 0) {                       // click an item
                    menu_sel = idx;
                    quit = desktop_dispatch(a, &open_menu, &menu_sel, &modal, &fm_sel);
                } else if (a == ACT_MENU_SYS)  open_menu = (open_menu == 1) ? 0 : 1;
                else if (a == ACT_MENU_APPS)   open_menu = (open_menu == 2) ? 0 : 2;
                else if (a == ACT_START)       open_menu = (open_menu == 3) ? 0 : 3;
                else                           open_menu = 0;   // click outside closes
                if (open_menu != prev_menu) menu_sel = 0;
            }
        } else {
            // Desk view: icon grid navigation (grid-generic).
            if (ev.type == EV_ARROW) {
                if (ev.ch == 'C') { if (sel % GRID_COLS != GRID_COLS - 1 && sel + 1 < ICON_COUNT) sel++; }
                else if (ev.ch == 'D') { if (sel % GRID_COLS != 0) sel--; }
                else if (ev.ch == 'B') { if (sel + GRID_COLS < ICON_COUNT) sel += GRID_COLS; }
                else if (ev.ch == 'A') { if (sel >= GRID_COLS) sel -= GRID_COLS; }
            } else if (ev.type == EV_KEY) {
                switch (ev.ch) {
                    case 'd': case 'D': if (sel % GRID_COLS != GRID_COLS - 1 && sel + 1 < ICON_COUNT) sel++; break;
                    case 'a': case 'A': if (sel % GRID_COLS != 0) sel--; break;
                    case 's': case 'S': if (sel + GRID_COLS < ICON_COUNT) sel += GRID_COLS; break;
                    case 'w': case 'W': if (sel >= GRID_COLS) sel -= GRID_COLS; break;
                    case '\t': sel = (sel + 1) % ICON_COUNT; break;
                    case 0x0D: case 'e': case 'E':
                        quit = desktop_dispatch(DESK_ICONS[sel].action, &open_menu, &menu_sel, &modal, &fm_sel);
                        break;
                    case '?': open_menu = 1; menu_sel = 0; break;
                    case 0x1B:
                        uart_puts(CLR HOME RESET "Exited desktop.\r\n");
                        return;
                    default: break;
                }
            } else if (ev.type == EV_MOUSE) {
                int a = region_hit(ev.row, ev.col);
                if (ev.ch == 0) {                            // hover highlights icon
                    for (int i = 0; i < ICON_COUNT; i++) if (DESK_ICONS[i].action == a) sel = i;
                } else {                                     // click
                    if (a == ACT_MENU_SYS) { open_menu = 1; menu_sel = 0; }
                    else if (a == ACT_MENU_APPS) { open_menu = 2; menu_sel = 0; }
                    else if (a == ACT_START) { open_menu = 3; menu_sel = 0; }
                    else if (a >= ACT_ICON_FILES && a <= ACT_ICON_ABOUT)
                        quit = desktop_dispatch(a, &open_menu, &menu_sel, &modal, &fm_sel);
                }
            }
        }

        if (quit == 1) return;      // exited to terminal (message already printed)
        if (quit == 2) break;       // shutdown

        int changed = force || sel != prev_sel || open_menu != prev_menu ||
                      menu_sel != prev_msel || modal != prev_modal || pki_sel != prev_pki ||
                      fm_sel != prev_fm;
        if (changed) desktop_redraw_full(sel, open_menu, menu_sel, modal, pki_sel, fm_sel);
    }
    uart_puts(CLR HOME RESET);
}

// === 8. EDIT — text editor ===

void cmd_edit(const char* arg) {
    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("edit: simple text editor\r\n"
                  "Usage: edit <filename>\r\n"
                  "Enter text line by line. Empty line (just Enter) saves and exits.\r\n"
                  "Backspace supported.\r\n");
        return;
    }

    char filename[VFS_MAX_PATH];
    k_strncpy(filename, arg, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    // Remove trailing whitespace
    int flen = k_strlen(filename);
    while (flen > 0 && (filename[flen-1] == ' ' || filename[flen-1] == '\t'))
        filename[--flen] = '\0';

    kprintf("Editing: %s\r\n", filename);
    uart_puts("Enter lines. Empty line saves & exits.\r\n");
    uart_puts("--- START ---\r\n");

    // Buffer for accumulating content
    char* content = (char*)mm_alloc(VFS_MAX_DATA);
    if (!content) { uart_puts("edit: out of memory\r\n"); return; }
    int pos = 0;
    content[0] = '\0';

    char line[128];
    while (pos < VFS_MAX_DATA - 128) {
        console_readline(line, sizeof(line));
        if (line[0] == '\0') break;  // empty line saves

        int llen = k_strlen(line);
        k_memcpy(content + pos, line, llen);
        pos += llen;
        content[pos++] = '\n';
        content[pos] = '\0';
    }

    // Ensure file exists
    if (!vfs_exists(filename)) {
        int r = vfs_mkfile(filename);
        if (r != E_OK) {
            kprintf("edit: cannot create file: %s\r\n", kerr_str(r));
            mm_free(content);
            return;
        }
    }
    int r = vfs_write(filename, content, pos);
    mm_free(content);
    if (r != E_OK)
        kprintf("edit: write error: %s\r\n", kerr_str(r));
    else
        kprintf("--- END --- (%d bytes written)\r\n", pos);
}
