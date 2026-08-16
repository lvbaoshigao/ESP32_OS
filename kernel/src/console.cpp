#include "kernel.h"
#include "wifi.h"

// === String utilities (declared in kernel.h, used everywhere) ===

int k_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }

int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int k_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) break;
        if (!b[i]) break;  // fix: catch b shorter than a
    }
    return 0;
}

char* k_strcpy(char* dst, const char* src) {
    char* r = dst; while ((*dst++ = *src++)); return r;
}

char* k_strncpy(char* dst, const char* src, int n) {
    char* r = dst;
    if (n <= 0) return r;
    while (--n > 0 && *src) *dst++ = *src++;
    *dst = '\0';
    return r;
}

void* k_memset(void* p, int c, int n) {
    unsigned char* d = (unsigned char*)p;
    while (n--) *d++ = (unsigned char)c;
    return p;
}

void* k_memcpy(void* dst, const void* src, int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

// === UART I/O (UART0 already initialized by bootloader) ===

void uart_putc(char c) {
    if (g_pipe_active) { pipe_putc(c); return; }
    volatile uint32_t* base = (volatile uint32_t*)UART0_BASE;
    while (((base[UART_STATUS_REG / 4] >> UART_TXFIFO_CNT_SHIFT) & UART_TXFIFO_CNT_MASK) >= UART_TX_FIFO_SIZE)
        ;
    *(volatile uint8_t*)(UART0_BASE + UART_FIFO_REG) = (uint8_t)c;
}

void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

int uart_avail() {
    return REG32(UART0_BASE + UART_STATUS_REG) & UART_RXFIFO_CNT_MASK;
}

int uart_getc() {
    while (!uart_avail())
        ;
    return *(volatile uint8_t*)(UART0_BASE + UART_FIFO_REG);
}

// === kprintf — supports %s %d %u %x %c %% with optional flags/width ===
//   flags:  '-' left-align, '0' zero-pad (numeric, right-align only)
//   width:  decimal digits, e.g. %08x, %02x, %4d, %03u, %-6s
// Renders numerics into a small buffer so padding is uniform.

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)

// Render %d/%u into out, return length.
static int fmt_int(char* out, int32_t v, int is_signed) {
    uint32_t u = (uint32_t)v;
    int n = 0, neg = 0;
    if (is_signed && v < 0) { neg = 1; u = ~u + 1; }
    char tmp[11];
    int i = 0;
    if (u == 0) tmp[i++] = '0';
    while (u) { tmp[i++] = '0' + (char)(u % 10); u /= 10; }
    if (neg) out[n++] = '-';
    while (i > 0) out[n++] = tmp[--i];
    return n;
}

// Render %x (uppercase) into out, return length.
static int fmt_hex(char* out, uint32_t v) {
    const char* hex = "0123456789ABCDEF";
    char tmp[8];
    int i = 0, n = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
    while (i > 0) out[n++] = tmp[--i];
    return n;
}

void kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { uart_putc(*fmt++); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        char conv = *fmt;
        char buf[16];
        int n = 0;

        if (conv == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = 0; while (s[len]) len++;
            if (!left) for (int i = len; i < width; i++) uart_putc(' ');
            uart_puts(s);
            if (left)  for (int i = len; i < width; i++) uart_putc(' ');
            fmt++;
            continue;
        } else if (conv == 'd' || conv == 'u') {
            n = fmt_int(buf, va_arg(ap, int32_t), conv == 'd');
        } else if (conv == 'x') {
            n = fmt_hex(buf, va_arg(ap, uint32_t));
        } else if (conv == 'c') {
            buf[0] = (char)va_arg(ap, int); n = 1;
        } else if (conv == '%') {
            buf[0] = '%'; n = 1;
        } else {
            // Unknown conversion — emit literally, don't consume an arg.
            uart_putc('%');
            if (conv) { uart_putc(conv); fmt++; }
            continue;
        }

        char padc = (zero && !left) ? '0' : ' ';
        if (!left && n < width) {
            if (padc == '0' && n > 0 && buf[0] == '-') {
                // keep sign before zero padding
                uart_putc('-');
                for (int i = n; i < width; i++) uart_putc('0');
                for (int i = 1; i < n; i++) uart_putc(buf[i]);
                fmt++;
                continue;
            }
            for (int i = n; i < width; i++) uart_putc(padc);
        }
        for (int i = 0; i < n; i++) uart_putc(buf[i]);
        if (left && n < width) for (int i = n; i < width; i++) uart_putc(' ');
        fmt++;
    }
    va_end(ap);
}

// === Shell state ===

char g_username[MAX_NAME_LEN] = "root";
char g_hostname[MAX_NAME_LEN] = "esp32c6";
static char g_divider = ' ';

void console_init() {}

void console_prompt() {
    kprintf("%s/%s:%s ", g_username, g_hostname, vfs_pwd());
}

int console_readline(char* buf, int maxlen) {
    // Persists across calls: a CRLF's LF arrives after this function returns.
    static int last_was_cr = 0;
    int pos = 0;
    while (pos < maxlen - 1) {
        // Yield while waiting for input (cooperative multitasking)
        while (!uart_avail()) {
            task_yield();
        }
        int c = uart_getc();
        // CRLF arrives as two bytes. Drop the LF that pairs with the preceding
        // CR, else it returns an extra empty line and prints a second prompt.
        if (c == '\n' && last_was_cr) { last_was_cr = 0; continue; }
        last_was_cr = (c == '\r');
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            break;
        }
        if (c == 0x7F || c == '\b') {
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");
            }
            continue;
        }
        if (c >= ' ' && c < 127) {
            buf[pos++] = (char)c;
            uart_putc((char)c);
        }
    }
    buf[pos] = '\0';
    return pos;
}

// === Tokenizer (supports chdiv-changeable delimiter) ===
// Shared non-static version declared in kernel.h

const char* tok_next(const char* s, char* out, int maxlen) {
    if (g_divider == ' ') {
        while (*s == ' ' || *s == '\t') s++;
    } else {
        while (*s == g_divider) s++;
    }
    int i = 0;
    if (g_divider == ' ') {
        while (*s && *s != ' ' && *s != '\t' && i < maxlen - 1)
            out[i++] = *s++;
    } else {
        while (*s && *s != g_divider && i < maxlen - 1)
            out[i++] = *s++;
    }
    out[i] = '\0';
    return s;
}

static int is_help(const char* arg) {
    return k_strcmp(arg, "-help") == 0;
}

// === Commands ===

static void cmd_help(const char*) {
    uart_puts("Commands:\r\n"
              "  ls, mkdir, newfile, cd, pwd, rename   — file management\r\n"
              "  cat, cp, rm, wc, hexdump, find        — file utilities\r\n"
              "  help, cmd, echo, env, export, chdiv, clear — shell utilities\r\n"
              "  whoami, user, su, ps                  — user & process management\r\n"
              "  part, disk, device, info, drivers, free — system information\r\n"
              "  wifi, net, com, btscan, wifisearch, wifiinfo — network & connectivity\r\n"
              "  pki, ping, dhcp, track, curl, desktop, edit — applications\r\n"
              "  calc, sleep, date                     — misc utilities\r\n"
              "  reboot, mode, log, version, uptime    — system control\r\n"
              "\r\nAppend -help to any command for details. Use 'cmd -list' for full list.\r\n");
}

static void ls_print(const char* name, int type, int size) {
    if (type == VFS_TYPE_DIR)
        kprintf("  [DIR]  %s\r\n", name);
    else
        kprintf("  [FILE] %s  (%d bytes)\r\n", name, size);
}

static void cmd_ls(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("ls: list files in the current directory\r\n"
                  "Usage: ls\r\n");
        return;
    }
    const char* path = (arg[0] && !is_help(arg)) ? arg : 0;
    int r = vfs_list(path, ls_print);
    if (r != E_OK)
        kprintf("ls: %s\r\n", kerr_str(r));
}

static void cmd_mkdir(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("mkdir: create a new directory\r\n"
                  "Usage: mkdir <name>\r\n"
                  "Example: mkdir new\r\n");
        return;
    }
    if (vfs_mkdir(arg) < 0)
        kprintf("mkdir: cannot create '%s'\r\n", arg);
}

static void cmd_com(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("com: show serial port connection status\r\n"
                  "Usage: com\r\n");
        return;
    }
    driver_t* d = driver_find("serial");
    kprintf("Serial (UART0): %s, 115200 8N1\r\n", (d && d->active) ? "connected" : "disconnected");
}

static void cmd_net(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("net: show network connection status\r\n"
                  "Usage: net\r\n");
        return;
    }
    driver_t* d = driver_find("network");
    if (d && d->active)
        uart_puts("Network: SLIP over UART0 (192.168.5.1)\r\n");
    else
        uart_puts("Network: not available (use 'mode network' to activate)\r\n");
}

static void cmd_newfile(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("newfile: create a new empty file\r\n"
                  "Usage: newfile <filename>\r\n"
                  "Example: newfile new.md\r\n");
        return;
    }
    if (vfs_mkfile(arg) < 0)
        kprintf("newfile: cannot create '%s'\r\n", arg);
}

static void cmd_pwd(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("pwd: print working directory\r\n"
                  "Usage: pwd\r\n");
        return;
    }
    kprintf("%s\r\n", vfs_pwd());
}

static void cmd_rename(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("rename: rename a file or directory\r\n"
                  "Usage: rename <oldname> <newname>\r\n");
        return;
    }
    char oldname[VFS_MAX_NAME], newname[VFS_MAX_NAME];
    const char* rest = tok_next(arg, oldname, sizeof(oldname));
    tok_next(rest, newname, sizeof(newname));
    if (!oldname[0] || !newname[0]) {
        uart_puts("rename: usage: rename <old> <new>\r\n");
        return;
    }
    if (vfs_rename(oldname, newname) < 0)
        uart_puts("rename: failed\r\n");
}

static void cmd_cd(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("cd: change working directory\r\n"
                  "Usage: cd <path>\r\n");
        return;
    }
    if (!arg[0]) { vfs_chdir("/"); return; }
    int r = vfs_chdir(arg);
    if (r != E_OK)
        kprintf("cd: %s: %s\r\n", arg, kerr_str(r));
}

static void cmd_disk(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("disk: view disk information\r\n"
                  "Usage: disk [-free | -part | -parted]\r\n"
                  "  -free   show available space\r\n"
                  "  -part   show partition status\r\n"
                  "  -parted perform partitioning\r\n");
        return;
    }
    if (!arg[0] || k_strcmp(arg, "-free") == 0) {
        kprintf("Heap: %u / %u bytes free\r\n", mm_free_bytes(), mm_total_bytes());
        // ponytail: no flash partition driver yet — add when S4 flash driver lands
        uart_puts("Flash: (partition driver not loaded)\r\n");
    } else if (k_strcmp(arg, "-part") == 0) {
        uart_puts("Partitions:\r\n"
                  "  boot       0x000000  4KB\r\n"
                  "  kernel     0x001000  (image)\r\n");
    } else if (k_strcmp(arg, "-parted") == 0) {
        uart_puts("disk -parted: not implemented\r\n");
    } else {
        kprintf("disk: unknown option '%s'\r\n", arg);
    }
}

static void cmd_device(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("device: show device information\r\n"
                  "Usage: device\r\n");
        return;
    }
    boot_params_t* bp = g_boot_params;
    uart_puts("Device: ESP32-C6 (QFN40)\r\n");
    uart_puts("CPU:    RISC-V 32-bit (RV32IMAC)\r\n");
    if (bp && bp->magic == BP_MAGIC_VALUE) {
        kprintf("Clock:  %uMHz\r\n", bp->hw_crystal / 1000000);
        kprintf("SRAM:   %uKB\r\n", bp->hw_sram_size / 1024);
        kprintf("Rev:    v0.%u\r\n", bp->hw_chip_rev);
    }
    kprintf("Heap:   %u / %u bytes free\r\n", mm_free_bytes(), mm_total_bytes());
}

static void cmd_chdiv(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("chdiv: change command argument delimiter\r\n"
                  "Usage: chdiv \"<char>\"\r\n"
                  "  Default is space. Example: chdiv \"/\"\r\n");
        return;
    }
    if (!arg[0]) {
        kprintf("Current delimiter: '%c' (0x%x)\r\n", g_divider, (uint32_t)g_divider);
        return;
    }
    const char* p = arg;
    if (*p == '"' || *p == '\'') p++;
    g_divider = *p;
    kprintf("Delimiter changed to '%c'\r\n", g_divider);
}

static void cmd_wifi(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("wifi: Wi-Fi status and control\r\n"
                  "Usage: wifi [-on | -off | -status | -help]\r\n"
                  "  (no args)      show current Wi-Fi info\r\n"
                  "  -on            start Wi-Fi access point (SSID: ESP32-OS)\r\n"
                  "  -off           stop Wi-Fi access point\r\n"
                  "  -status        show detailed Wi-Fi status\r\n"
                  "  -nf            read hardware noise floor (reset-safe)\r\n"
                  "  -scan          per-channel scan (unavailable: see note)\r\n"
                  "  -cal           info on ROM PHY calibration limitation\r\n"
                  "  -help          show this help\r\n");
        return;
    }
    if (!arg[0]) {
        if (wifi_is_active()) {
            uart_puts("Wi-Fi: AP ACTIVE\r\n");
            uart_puts("  NOTE: configured, but no beacon on air until PHY is calibrated\r\n");
            uart_puts("Use 'wifi -status' for details.\r\n");
        } else if (wifi_ap_configured()) {
            uart_puts("Wi-Fi: AP configured (SSID: ESP32-OS)\r\n");
            uart_puts("  NOTE: no beacon on air until PHY is calibrated\r\n");
            uart_puts("Use 'wifi -status' for details.\r\n");
        } else if (wifi_fw_loaded()) {
            uart_puts("Wi-Fi: radio powered, no AP running\r\n");
            uart_puts("Use 'wifi -on' to configure the access point.\r\n");
        } else {
            uart_puts("Wi-Fi: driver not ready\r\n");
        }
        return;
    }
    if (k_strcmp(arg, "-on") == 0) {
        int r = wifi_ap_start("ESP32-OS", 0, 6);
        if (r == 0) {
            uart_puts("Wi-Fi AP configured (SSID: ESP32-OS)\r\n");
            uart_puts("  no beacon on air until PHY is calibrated\r\n");
        } else {
            uart_puts("Wi-Fi AP start failed\r\n");
        }
    } else if (k_strcmp(arg, "-off") == 0) {
        wifi_ap_stop();
        uart_puts("Wi-Fi AP stopped, output switched to serial\r\n");
        display_set_mode(DISPLAY_SERIAL);
    } else if (k_strcmp(arg, "-status") == 0) {
        soft_wifiinfo("");
    } else if (k_strcmp(arg, "-cal") == 0) {
        // Empirically, the bare-metal ROM PHY sequence RESETS the ESP32-C6
        // (verified: wifi_rf_phy_enable and set_channel_rfpll both reset the
        // chip without ESP-IDF's register_chipv7_phy() init-data setup).
        // Keep it as an explicit, opt-in diagnostic only.
        uart_puts("wifi -cal: the ROM PHY calibration path RESETS this chip on\r\n"
                  "bare-metal ESP32-C6 (no ESP-IDF phy_init_data). This is a\r\n"
                  "known hardware limitation. Run 'wifi -cal force' to reproduce\r\n"
                  "(the board WILL reset).\r\n");
    } else if (k_strcmp(arg, "-cal force") == 0) {
        uart_puts("Wi-Fi: running ROM PHY calibration (verbose) — expect reset...\r\n");
        wifi_phy_calibrate_verbose();
        uart_puts("Wi-Fi: PHY calibration returned OK (unexpected — no reset)\r\n");
    } else if (k_strcmp(arg, "-nf") == 0) {
        if (!wifi_fw_loaded()) { uart_puts("Wi-Fi: radio not powered\r\n"); return; }
        kprintf("Wi-Fi: noise floor = %d (0 = PHY uncalibrated)\r\n",
                wifi_read_noisefloor());
    } else if (k_strcmp(arg, "-scan") == 0) {
        // AP/energy scan requires a calibrated PHY. On bare-metal ESP32-C6 the
        // ROM RF/PHY calls reset the chip (see 'wifi -cal'), so no scan path is
        // available without ESP-IDF. Report honestly rather than crash.
        uart_puts("wifi -scan: not available. Scanning needs a calibrated PHY,\r\n"
                  "but the ESP32-C6 ROM RF/PHY calls reset the chip when called\r\n"
                  "bare-metal (no ESP-IDF). See 'wifi -cal' and\r\n"
                  "AI/AI_project/think/01_wifi_scan_feasibility_en.md.\r\n");
    } else {
        kprintf("wifi: unknown option '%s'. Use 'wifi -help'\r\n", arg);
    }
}

static void cmd_reboot(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("reboot: reboot the device\r\n"
                  "Usage: reboot\r\n");
        return;
    }
    uart_puts("Writing mailbox and rebooting...\r\n");
    mailbox_t* mb = (mailbox_t*)MAILBOX_ADDR;
    mb->magic = MB_MAGIC_VALUE;
    mb->status = KERN_STATUS_REBOOT;
    mb->request = BOOT_MODE_NORMAL;
    mb->retry = 0;

    // Flush UART TX
    while ((REG32(UART0_BASE + UART_STATUS_REG) >> UART_TXFIFO_CNT_SHIFT) & UART_TXFIFO_CNT_MASK)
        ;

    system_reset();
}

static void cmd_info(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("info: show boot parameters\r\n"
                  "Usage: info\r\n");
        return;
    }
    boot_params_t* bp = g_boot_params;
    if (!bp || bp->magic != BP_MAGIC_VALUE) {
        uart_puts("boot_params invalid\r\n");
        return;
    }
    static const char* bm[] = {"normal","safe","recovery","factory"};
    static const char* bs[] = {"main_flash","backup","external"};
    kprintf("Boot params (API v%d.%d):\r\n", bp->api_version >> 16, bp->api_version & 0xFFFF);
    kprintf("  mode:    %s\r\n", bp->boot_mode < 4 ? bm[bp->boot_mode] : "?");
    kprintf("  source:  %s\r\n", bp->boot_source < 3 ? bs[bp->boot_source] : "?");
    kprintf("  crystal: %uMHz, SRAM: %uKB\r\n", bp->hw_crystal / 1000000, bp->hw_sram_size / 1024);
    kprintf("  entry:   0x%08x, load: 0x%08x, size: %u\r\n", bp->kern_entry, bp->kern_load, bp->kern_size);
    kprintf("  retries: %u\r\n", bp->retry_count);
}

// ponytail: Direct Boot runs at XTAL speed (40MHz), not PLL (160MHz)
// CYCLES_PER_US defined in kernel.h

static void cmd_uptime(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("uptime: show system uptime\r\n"
                  "Usage: uptime\r\n");
        return;
    }
    uint32_t cycles = get_mcycle();
    uint32_t us = cycles / CYCLES_PER_US;
    uint32_t sec = us / 1000000;
    uint32_t ms = (us % 1000000) / 1000;
    kprintf("Uptime: %u.%03us (%u cycles)\r\n", sec, ms, cycles);
}

static void cmd_drivers(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("drivers: list registered drivers\r\n"
                  "Usage: drivers\r\n");
        return;
    }
    extern driver_t g_drivers[];
    kprintf("Registered drivers:\r\n");
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (g_drivers[i].name)
            kprintf("  %s  %s\r\n", g_drivers[i].name, g_drivers[i].active ? "[active]" : "[inactive]");
    }
}

static void cmd_log(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("log: show kernel boot log\r\n"
                  "Usage: log\r\n");
        return;
    }
    if (g_boot_log_pos == 0) {
        uart_puts("(boot log empty)\r\n");
        return;
    }
    uart_puts("--- Boot log ---\r\n");
    uart_puts(g_boot_log);
    uart_puts("--- End ---\r\n");
}

static void cmd_version(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("version: show kernel version\r\n"
                  "Usage: version\r\n");
        return;
    }
    uart_puts("ESPOS Kernel v0.1.0 (ESP32-C6, rv32imac)\r\n");
}

static void cmd_mode(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("mode: get/set display output mode\r\n"
                  "Usage: mode [serial|network|video]\r\n"
                  "  serial  console UART mode (default)\r\n"
                  "  network SLIP over UART (192.168.5.1)\r\n");
        return;
    }
    static const char* names[] = {"serial","network","video"};
    if (!arg[0]) {
        kprintf("Display mode: %s\r\n", names[display_get_mode()]);
        return;
    }
    for (int i = 0; i < DISPLAY_MAX; i++) {
        if (k_strcmp(arg, names[i]) == 0) {
            int r = display_set_mode(i);
            kprintf(r == 0 ? "Mode set to %s\r\n" : "Mode %s not available\r\n", names[i]);
            return;
        }
    }
    uart_puts("Unknown mode. Use: serial, network, video\r\n");
}

// === Environment variables ===
static env_var_t g_env_table[ENV_MAX];
static int g_env_count = 0;

void env_init() {
    g_env_count = 0;
    env_set("PATH", "/app:/system");
    env_set("HOME", "/user/root");
    env_set("USER", "root");
}

const char* env_get(const char* name) {
    for (int i = 0; i < g_env_count; i++)
        if (k_strcmp(g_env_table[i].name, name) == 0) return g_env_table[i].value;
    return 0;
}

int env_set(const char* name, const char* value) {
    for (int i = 0; i < g_env_count; i++) {
        if (k_strcmp(g_env_table[i].name, name) == 0) {
            k_strncpy(g_env_table[i].value, value, ENV_VAL_LEN - 1);
            return E_OK;
        }
    }
    if (g_env_count >= ENV_MAX) return E_NOSPC;
    k_strncpy(g_env_table[g_env_count].name, name, ENV_NAME_LEN - 1);
    k_strncpy(g_env_table[g_env_count].value, value, ENV_VAL_LEN - 1);
    g_env_count++;
    return E_OK;
}

int env_count() { return g_env_count; }
env_var_t* env_entry(int idx) { return (idx >= 0 && idx < g_env_count) ? &g_env_table[idx] : 0; }

// Remove a variable entirely (real unexport). Returns E_OK or E_NOENT.
static int env_unset(const char* name) {
    for (int i = 0; i < g_env_count; i++) {
        if (k_strcmp(g_env_table[i].name, name) == 0) {
            for (int j = i; j < g_env_count - 1; j++)
                g_env_table[j] = g_env_table[j + 1];
            g_env_count--;
            return E_OK;
        }
    }
    return E_NOENT;
}

// === User management ===
int g_user_level = USER_LEVEL_ROOT;

int user_check_root() { return g_user_level == USER_LEVEL_ROOT ? 1 : 0; }

static int passwd_append(const char* name, const char* level) {
    char buf[256];
    int len = vfs_read("/system/passwd", buf, sizeof(buf) - 1);
    if (len < 0) len = 0;
    int nlen = k_strlen(name);
    int llen = k_strlen(level);
    if (len + nlen + llen + 3 > (int)sizeof(buf)) return E_NOSPC;
    k_memcpy(buf + len, name, nlen);
    buf[len + nlen] = ':';
    k_memcpy(buf + len + nlen + 1, level, llen);
    buf[len + nlen + 1 + llen] = '\n';
    len += nlen + llen + 2;
    buf[len] = '\0';
    return vfs_write("/system/passwd", buf, len);
}

int user_add(const char* name) {
    char path[VFS_MAX_PATH];
    k_strcpy(path, "/user/");
    int plen = 6;
    int nlen = k_strlen(name);
    k_memcpy(path + plen, name, nlen);
    path[plen + nlen] = '\0';
    if (vfs_exists(path)) return E_EXIST;

    int r = vfs_mkdir(path);
    if (r != E_OK) return r;

    k_strcpy(path + plen + nlen, "/desktop");
    vfs_mkdir(path);
    k_strcpy(path + plen + nlen, "/download");
    vfs_mkdir(path);

    return passwd_append(name, "user");
}

int user_del(const char* name) {
    if (k_strcmp(name, "root") == 0) return E_ACCES;
    // ponytail: delete user dir recursively needs work — just remove passwd entry for now
    // Real recursive delete requires walking all children
    char buf[256];
    int len = vfs_read("/system/passwd", buf, sizeof(buf) - 1);
    if (len < 0) return E_IO;
    buf[len] = '\0';

    // Rebuild passwd without this user
    char newbuf[256];
    int npos = 0;
    char* line = buf;
    while (*line) {
        char* nl = line;
        while (*nl && *nl != '\n') nl++;
        int llen = nl - line;
        // Check if this line starts with "name:"
        int nlen = k_strlen(name);
        if (llen > nlen && line[nlen] == ':' && k_strncmp(line, name, nlen) == 0) {
            // Skip this line
        } else if (llen > 0) {
            k_memcpy(newbuf + npos, line, llen);
            newbuf[npos + llen] = '\n';
            npos += llen + 1;
        }
        line = *nl ? nl + 1 : nl;
    }
    newbuf[npos] = '\0';
    vfs_write("/system/passwd", newbuf, npos);
    return E_OK;
}

int user_set_perm(const char* name, int level) {
    const char* lvl = (level == USER_LEVEL_ROOT) ? "root" : "user";
    // Rewrite passwd with updated permission
    char buf[256];
    int len = vfs_read("/system/passwd", buf, sizeof(buf) - 1);
    if (len < 0) return E_IO;
    buf[len] = '\0';

    char newbuf[256];
    int npos = 0;
    int found = 0;
    char* line = buf;
    while (*line) {
        char* nl = line;
        while (*nl && *nl != '\n') nl++;
        int llen = nl - line;
        int nlen = k_strlen(name);
        if (llen > nlen && line[nlen] == ':' && k_strncmp(line, name, nlen) == 0) {
            k_memcpy(newbuf + npos, name, nlen);
            newbuf[npos + nlen] = ':';
            int lvl_len = k_strlen(lvl);
            k_memcpy(newbuf + npos + nlen + 1, lvl, lvl_len);
            newbuf[npos + nlen + 1 + lvl_len] = '\n';
            npos += nlen + 1 + lvl_len + 1;
            found = 1;
        } else if (llen > 0) {
            k_memcpy(newbuf + npos, line, llen);
            newbuf[npos + llen] = '\n';
            npos += llen + 1;
        }
        line = *nl ? nl + 1 : nl;
    }
    if (!found) return E_NOENT;
    newbuf[npos] = '\0';
    vfs_write("/system/passwd", newbuf, npos);
    return E_OK;
}

int user_switch(const char* name) {
    char buf[256];
    int len = vfs_read("/system/passwd", buf, sizeof(buf) - 1);
    if (len < 0) return E_IO;
    buf[len] = '\0';

    char* line = buf;
    while (*line) {
        char* nl = line;
        while (*nl && *nl != '\n') nl++;
        int nlen = k_strlen(name);
        if ((nl - line) > nlen && line[nlen] == ':' && k_strncmp(line, name, nlen) == 0) {
            k_strncpy(g_username, name, MAX_NAME_LEN - 1);
            const char* perm = line + nlen + 1;
            g_user_level = (k_strncmp(perm, "root", 4) == 0) ? USER_LEVEL_ROOT : USER_LEVEL_USER;
            env_set("USER", name);
            char home[VFS_MAX_PATH];
            k_strcpy(home, "/user/");
            k_memcpy(home + 6, name, nlen);
            home[6 + nlen] = '\0';
            env_set("HOME", home);
            return E_OK;
        }
        line = *nl ? nl + 1 : nl;
    }
    return E_NOENT;
}

// === Pipe support ===
char* g_pipe_buf = 0;
int g_pipe_pos = 0;
int g_pipe_active = 0;

void pipe_putc(char c) {
    if (g_pipe_buf && g_pipe_pos < PIPE_BUF_SIZE - 1)
        g_pipe_buf[g_pipe_pos++] = c;
}

// === New commands ===

static void cmd_user(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("user: manage system users\r\n"
                  "Usage: user -add <name> | -del <name> | -per <name> <root|user>\r\n");
        return;
    }
    char sub[16], name[32], perm[16];
    const char* p = tok_next(arg, sub, sizeof(sub));
    p = tok_next(p, name, sizeof(name));

    if (k_strcmp(sub, "-add") == 0) {
        if (!name[0]) { uart_puts("user: missing username\r\n"); return; }
        int r = user_add(name);
        if (r != E_OK) kprintf("user: %s\r\n", kerr_str(r));
        else kprintf("User '%s' created\r\n", name);
    } else if (k_strcmp(sub, "-del") == 0) {
        if (!user_check_root()) { uart_puts("user: Permission denied (EACCES)\r\n"); return; }
        if (!name[0]) { uart_puts("user: missing username\r\n"); return; }
        int r = user_del(name);
        if (r != E_OK) kprintf("user: %s\r\n", kerr_str(r));
        else kprintf("User '%s' deleted\r\n", name);
    } else if (k_strcmp(sub, "-per") == 0) {
        if (!user_check_root()) { uart_puts("user: Permission denied (EACCES)\r\n"); return; }
        tok_next(p, perm, sizeof(perm));
        if (!name[0] || !perm[0]) { uart_puts("user: usage: user -per <name> <root|user>\r\n"); return; }
        int lvl = (k_strcmp(perm, "root") == 0) ? USER_LEVEL_ROOT : USER_LEVEL_USER;
        int r = user_set_perm(name, lvl);
        if (r != E_OK) kprintf("user: %s\r\n", kerr_str(r));
        else kprintf("User '%s' permission set to %s\r\n", name, perm);
    } else {
        kprintf("user: unknown option '%s'\r\n", sub);
    }
}

static void cmd_su(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("su: switch user\r\nUsage: su <username>\r\n");
        return;
    }
    char name[32];
    tok_next(arg, name, sizeof(name));
    int r = user_switch(name);
    if (r != E_OK) kprintf("su: user '%s' not found\r\n", name);
}

static void cmd_export(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("export: set environment variable\r\n"
                  "Usage:\r\n"
                  "  export VAR=value    set and export variable\r\n"
                  "  export VAR          export existing variable to environment\r\n"
                  "  export -n VAR       unset (delete) variable\r\n");
        return;
    }

    // Check for -n flag (unset)
    if (k_strncmp(arg, "-n ", 3) == 0) {
        char name[ENV_NAME_LEN];
        tok_next(arg + 3, name, sizeof(name));
        if (!name[0]) { uart_puts("export -n: missing variable name\r\n"); return; }
        int r = env_unset(name);   // real removal, not just an empty value
        if (r == E_OK)      kprintf("export: '%s' unset\r\n", name);
        else                kprintf("export: '%s' is not set\r\n", name);
        return;
    }

    // Check for '='
    char name[ENV_NAME_LEN], value[ENV_VAL_LEN];
    const char* eq = arg;
    while (*eq && *eq != '=') eq++;

    if (*eq == '=') {
        // VAR=value
        int nlen = eq - arg;
        if (nlen >= ENV_NAME_LEN) nlen = ENV_NAME_LEN - 1;
        k_memcpy(name, arg, nlen);
        name[nlen] = '\0';
        k_strncpy(value, eq + 1, ENV_VAL_LEN - 1);
        int r = env_set(name, value);
        if (r != E_OK) kprintf("export: %s\r\n", kerr_str(r));
    } else {
        // Just "export VAR" — ensure it exists
        k_strncpy(name, arg, ENV_NAME_LEN - 1);
        const char* existing = env_get(name);
        if (existing) {
            kprintf("export: '%s' already exported (%s)\r\n", name, existing);
        } else {
            kprintf("export: '%s' is not set\r\n", name);
        }
    }
}

static void cmd_env(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("env: list all environment variables\r\nUsage: env\r\n");
        return;
    }
    for (int i = 0; i < env_count(); i++) {
        env_var_t* e = env_entry(i);
        if (e) kprintf("%s=%s\r\n", e->name, e->value);
    }
}

static void cmd_echo(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("echo: print text or variable\r\n"
                  "Usage: echo <text>  or  echo $VAR\r\n");
        return;
    }
    const char* p = arg;
    while (*p) {
        if (*p == '$') {
            p++;
            char vname[ENV_NAME_LEN];
            int vi = 0;
            while (*p && *p != ' ' && *p != '\t' && vi < ENV_NAME_LEN - 1)
                vname[vi++] = *p++;
            vname[vi] = '\0';
            const char* val = env_get(vname);
            if (val) uart_puts(val);
        } else {
            uart_putc(*p++);
        }
    }
    uart_puts("\r\n");
}

static void cmd_ps(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("ps: list running processes\r\n"
                  "Usage: ps [-a] [-l]\r\n");
        return;
    }
    int n = task_count();
    if (arg[0] && k_strcmp(arg, "-l") == 0) {
        uart_puts("PID  STATE  NAME\r\n");
        for (int i = 0; i < n; i++) {
            task_t* t = task_get(i);
            if (!t) continue;
            const char* st = "?";
            if (t->state == TASK_READY)   st = "ready";
            if (t->state == TASK_RUNNING) st = "run";
            if (t->state == TASK_BLOCKED) st = "block";
            if (t->state == TASK_EXIT)    st = "exit";
            kprintf("%-4d %-6s %s\r\n", i, st, t->name);
        }
    } else {
        uart_puts("PID  NAME\r\n");
        for (int i = 0; i < n; i++) {
            task_t* t = task_get(i);
            if (t) kprintf("%-4d %s\r\n", i, t->name);
        }
    }
}

// cmd_wifisearch/wifiinfo: externs defined in software.cpp; dispatch table uses soft_wifisearch/soft_wifiinfo
// (static versions removed — they were shadowed by the extern declarations)

static void cmd_uartstat(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("uartstat: show UART0 status register\r\nUsage: uartstat\r\n");
        return;
    }
    uint32_t status = REG32(UART0_BASE + UART_STATUS_REG);
    uint32_t rxfifo = status & 0xFF;
    uint32_t txfifo = (status >> 16) & 0xFF;
    kprintf("UART_STATUS=0x%08x  RX_FIFO=%u  TX_FIFO=%u\r\n", status, rxfifo, txfifo);

    // Also dump UART_CONF0 and CLK_CONF
    uint32_t conf0 = REG32(UART0_BASE + UART_CONF0_REG);   // 0x0020
    uint32_t clk_conf = REG32(UART0_BASE + UART_CLK_CONF_REG); // 0x0088
    kprintf("UART_CONF0=0x%08x  UART_CLK_CONF=0x%08x\r\n", conf0, clk_conf);

    // PCR UART0 clock status
    uint32_t pcr_conf = REG32(0x60096000 + 0x0000);  // PCR_UART0_CONF
    uint32_t pcr_sclk = REG32(0x60096000 + 0x0004);  // PCR_UART0_SCLK_CONF
    kprintf("PCR_UART0_CONF=0x%08x  PCR_UART0_SCLK=0x%08x\r\n", pcr_conf, pcr_sclk);

    // Read UART_FIFO directly to see if there's data
    if (rxfifo > 0) {
        uart_puts("RX data: ");
        for (uint32_t i = 0; i < rxfifo && i < 16; i++) {
            uint8_t c = *(volatile uint8_t*)(UART0_BASE + UART_FIFO_REG);
            kprintf("%02x ", c);
        }
        uart_puts("\r\n");
    }
}

static void cmd_btscan(const char* arg) {
    if (arg[0] && is_help(arg)) {
        uart_puts("btscan: scan for nearby Bluetooth devices\r\n"
                  "Usage: btscan\r\n"
                  "Output: device name, MAC address, RSSI\r\n");
        return;
    }

    if (!bt_is_active()) {
        uart_puts("btscan: Bluetooth radio not powered\r\n");
        return;
    }

    if (bt_is_scanning()) {
        uart_puts("btscan: discovery already in progress\r\n");
        return;
    }

    uart_puts("Scanning for BLE devices...\r\n");

    struct bt_device_t results[MAX_BT_RESULTS];
    k_memset(results, 0, sizeof(results));

    int count = bt_scan(results, MAX_BT_RESULTS);

    if (count < 0) {
        uart_puts("btscan: scan failed\r\n");
        return;
    }

    if (count == 0) {
        uart_puts("No BLE devices found (scan complete, none in range)\r\n");
        return;
    }

    uart_puts("BLE Devices:\r\n");
    uart_puts("  Name                              MAC              RSSI\r\n");
    uart_puts("  ----                              ---              ----\r\n");

    for (int i = 0; i < count; i++) {
        uart_puts("  ");
        if (results[i].name[0]) {
            int max_print = 32;
            for (int j = 0; j < max_print && results[i].name[j]; j++)
                uart_putc(results[i].name[j]);
            // Pad to 32 chars
            for (int j = k_strlen(results[i].name); j < 32; j++) uart_putc(' ');
        } else {
            uart_puts("(unnamed)                       ");
        }

        kprintf(" %02x:%02x:%02x:%02x:%02x:%02x",
                results[i].addr[0], results[i].addr[1], results[i].addr[2],
                results[i].addr[3], results[i].addr[4], results[i].addr[5]);

        if (results[i].rssi)
            kprintf("  %4d\r\n", results[i].rssi);
        else
            uart_puts("  ----\r\n");
    }

    kprintf("  (%d device(s) found)\r\n", count);
}

static void cmd_pki_handler(const char* arg) { cmd_pki(arg); }

// === Dispatch table struct (declared early for cmd_cmd) ===

struct cmd_entry {
    const char* name;
    void (*handler)(const char* arg);
};

// Forward declarations for dispatch table ===
extern void cmd_part(const char* arg);
extern "C" void cmd_gui(const char* arg);
extern cmd_entry commands[];
// coreutils.cpp
extern void cmd_cat(const char*);   extern void cmd_rm(const char*);
extern void cmd_cp(const char*);    extern void cmd_clear(const char*);
extern void cmd_whoami(const char*); extern void cmd_free(const char*);
extern void cmd_hexdump(const char*); extern void cmd_wc(const char*);
extern void cmd_find(const char*);  extern void cmd_calc(const char*);
extern void cmd_sleep(const char*); extern void cmd_date(const char*);

// === cmd command: list and unregister commands ===

static int is_system_cmd(const char* name) {
    static const char* system_cmds[] = {
        "help", "cmd", "ls", "mkdir", "newfile", "pwd", "rename", "cd",
        "chdiv", "echo", "env", "export", "su", "user", "ps",
        "part", "disk", "device", "drivers", "info", "log", "version", "uptime",
        "reboot", "mode", "wifi", "net", "com", "btscan", "uartstat",
        "pki", "wifisearch", "wifiinfo",
        "ping", "dhcp", "track", "curl", "desktop", "edit", "gui",
        "cat", "rm", "cp", "clear", "whoami", "free", "hexdump", "wc",
        "find", "calc", "sleep", "date", 0
    };
    for (int i = 0; system_cmds[i]; i++)
        if (k_strcmp(name, system_cmds[i]) == 0) return 1;
    return 0;
}

static void cmd_cmd(const char* arg) {
    if (!arg[0] || is_help(arg)) {
        uart_puts("cmd: command management\r\n"
                  "Usage:\r\n"
                  "  cmd -list          list all registered commands\r\n"
                  "  cmd -unregister <name>  remove a third-party command\r\n"
                  "  cmd -help          show this help\r\n");
        return;
    }

    char sub[16], name[32];
    const char* p = tok_next(arg, sub, sizeof(sub));

    if (k_strcmp(sub, "-list") == 0) {
        uart_puts("Registered commands:\r\n");
        for (int i = 0; commands[i].name; i++) {
            if (!commands[i].handler) continue;  // skip unregistered
            kprintf("  %s%s\r\n", commands[i].name,
                    is_system_cmd(commands[i].name) ? "  [system]" : "  [user]");
        }
    } else if (k_strcmp(sub, "-unregister") == 0) {
        tok_next(p, name, sizeof(name));
        if (!name[0]) { uart_puts("cmd -unregister: missing command name\r\n"); return; }

        if (is_system_cmd(name)) {
            kprintf("cmd: '%s' is a system command and cannot be removed\r\n", name);
            return;
        }

        for (int i = 0; commands[i].name; i++) {
            if (commands[i].handler && k_strcmp(commands[i].name, name) == 0) {
                commands[i].handler = 0;  // mark as unregistered (keep name for listing)
                kprintf("cmd: unregistered '%s'\r\n", name);
                return;
            }
        }
        kprintf("cmd: command '%s' not found\r\n", name);
    } else {
        kprintf("cmd: unknown option '%s'. Use 'cmd -help'\r\n", sub);
    }
}

// === Dispatch ===

cmd_entry commands[] = {
    {"help",       cmd_help},
    {"cmd",        cmd_cmd},
    {"gui",        cmd_gui},
    {"ls",         cmd_ls},
    {"mkdir",      cmd_mkdir},
    {"com",        cmd_com},
    {"net",        cmd_net},
    {"newfile",    cmd_newfile},
    {"pwd",        cmd_pwd},
    {"rename",     cmd_rename},
    {"cd",         cmd_cd},
    {"part",       cmd_part},
    {"disk",       cmd_disk},
    {"device",     cmd_device},
    {"chdiv",      cmd_chdiv},
    {"wifi",       cmd_wifi},
    {"reboot",     cmd_reboot},
    {"info",       cmd_info},
    {"uptime",     cmd_uptime},
    {"mode",       cmd_mode},
    {"drivers",    cmd_drivers},
    {"log",        cmd_log},
    {"version",    cmd_version},
    {"user",       cmd_user},
    {"su",         cmd_su},
    {"export",     cmd_export},
    {"env",        cmd_env},
    {"echo",       cmd_echo},
    {"ps",         cmd_ps},
    {"pki",        cmd_pki_handler},
    {"wifisearch", soft_wifisearch},
    {"wifiinfo",   soft_wifiinfo},
    {"btscan",     cmd_btscan},
    {"uartstat",   cmd_uartstat},
    {"ping",       cmd_ping},
    {"dhcp",       cmd_dhcp},
    {"track",      cmd_track},
    {"curl",       cmd_curl},
    {"desktop",    cmd_desktop},
    {"edit",       cmd_edit},
    {"cat",        cmd_cat},
    {"rm",         cmd_rm},
    {"cp",         cmd_cp},
    {"clear",      cmd_clear},
    {"whoami",     cmd_whoami},
    {"free",       cmd_free},
    {"hexdump",    cmd_hexdump},
    {"wc",         cmd_wc},
    {"find",       cmd_find},
    {"calc",       cmd_calc},
    {"sleep",      cmd_sleep},
    {"date",       cmd_date},
    {0, 0}
};

static void dispatch_single(const char* line) {
    if (g_divider == ' ') {
        while (*line == ' ' || *line == '\t') line++;
    } else {
        while (*line == g_divider) line++;
    }
    if (!*line) return;

    char cmd[32];
    const char* rest = tok_next(line, cmd, sizeof(cmd));
    char arg[128];
    if (g_divider == ' ') {
        while (*rest == ' ' || *rest == '\t') rest++;
    } else {
        while (*rest == g_divider) rest++;
    }
    k_strncpy(arg, rest, sizeof(arg) - 1);
    arg[sizeof(arg) - 1] = '\0';

    for (int i = 0; commands[i].name; i++) {
        if (commands[i].handler && k_strcmp(cmd, commands[i].name) == 0) {
            commands[i].handler(arg);
            return;
        }
    }
    kprintf("Unknown command: %s\r\n", cmd);
}

void console_dispatch(const char* line) {
    // Check for pipe '|'
    const char* pipe = line;
    while (*pipe && *pipe != '|') pipe++;

    if (!*pipe) {
        dispatch_single(line);
        return;
    }

    // Split on '|', run left side with output to pipe buffer
    char left[128];
    int llen = pipe - line;
    if (llen >= (int)sizeof(left)) llen = sizeof(left) - 1;
    k_memcpy(left, line, llen);
    left[llen] = '\0';

    g_pipe_buf = (char*)mm_alloc(PIPE_BUF_SIZE);
    if (!g_pipe_buf) { uart_puts("pipe: out of memory\r\n"); return; }
    g_pipe_pos = 0;
    g_pipe_active = 1;

    dispatch_single(left);

    g_pipe_active = 0;
    g_pipe_buf[g_pipe_pos] = '\0';

    // Feed pipe output as context for right side
    // Print it then dispatch right side
    const char* right = pipe + 1;
    uart_puts(g_pipe_buf);

    mm_free(g_pipe_buf);
    g_pipe_buf = 0;

    // Dispatch the right side normally
    dispatch_single(right);
}
