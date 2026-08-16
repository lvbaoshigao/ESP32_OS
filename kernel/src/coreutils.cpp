#include "kernel.h"

// === Core utility commands ===
// File and system utilities layered on the existing VFS / MM APIs. No register
// access, no ESP-IDF — pure kernel-service consumers. Registered in the shell
// dispatch table in console.cpp. See AI/AI_project/update3_commands_desktop_en.md

extern char g_username[];   // defined in console.cpp

// --- small local helpers ----------------------------------------------------
static int cu_is_help(const char* a) { return a[0] && k_strcmp(a, "-help") == 0; }

// Copy arg into buf, trimming trailing whitespace (filenames come with none,
// but a stray space would break VFS lookups).
static void cu_arg1(const char* arg, char* buf, int max) {
    k_strncpy(buf, arg, max - 1);
    buf[max - 1] = '\0';
    int n = k_strlen(buf);
    while (n > 0 && (buf[n-1] == ' ' || buf[n-1] == '\t')) buf[--n] = '\0';
}

// Split arg into two whitespace-separated tokens. Returns token count.
static int cu_arg2(const char* arg, char* a, int amax, char* b, int bmax) {
    const char* p = arg;
    while (*p == ' ' || *p == '\t') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < amax - 1) a[i++] = *p++;
    a[i] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    int j = 0;
    while (*p && *p != ' ' && *p != '\t' && j < bmax - 1) b[j++] = *p++;
    b[j] = '\0';
    return (i ? 1 : 0) + (j ? 1 : 0);
}

// Emit text translating bare LF to CRLF so it renders cleanly on the terminal.
static void cu_puttext(const char* buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') uart_putc('\r');
        uart_putc(buf[i]);
    }
}

static int cu_atoi(const char* s, const char** end) {
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (end) *end = s;
    return v * sign;
}

// substring match (needle in haystack)
static int cu_contains(const char* hay, const char* needle) {
    if (!needle[0]) return 1;
    for (const char* h = hay; *h; h++) {
        const char* a = h; const char* b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

// --- cat --------------------------------------------------------------------
void cmd_cat(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("cat: print file contents\r\nUsage: cat <file>\r\n");
        return;
    }
    char path[VFS_MAX_PATH];
    cu_arg1(arg, path, sizeof(path));
    static char buf[VFS_MAX_DATA];
    int len = vfs_read(path, buf, sizeof(buf));
    if (len < 0) { kprintf("cat: %s: %s\r\n", path, kerr_str(len)); return; }
    cu_puttext(buf, len);
    if (len == 0 || buf[len-1] != '\n') uart_puts("\r\n");
}

// --- rm ---------------------------------------------------------------------
void cmd_rm(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("rm: remove a file or directory\r\nUsage: rm <path>\r\n");
        return;
    }
    char path[VFS_MAX_PATH];
    cu_arg1(arg, path, sizeof(path));
    int r = vfs_delete(path);
    if (r != E_OK) kprintf("rm: %s: %s\r\n", path, kerr_str(r));
}

// --- cp ---------------------------------------------------------------------
void cmd_cp(const char* arg) {
    char src[VFS_MAX_PATH], dst[VFS_MAX_PATH];
    if (!arg[0] || cu_is_help(arg) || cu_arg2(arg, src, sizeof(src), dst, sizeof(dst)) < 2) {
        uart_puts("cp: copy a file\r\nUsage: cp <src> <dst>\r\n");
        return;
    }
    static char buf[VFS_MAX_DATA];
    int len = vfs_read(src, buf, sizeof(buf));
    if (len < 0) { kprintf("cp: %s: %s\r\n", src, kerr_str(len)); return; }
    if (vfs_exists(dst)) { kprintf("cp: %s: already exists\r\n", dst); return; }
    int r = vfs_mkfile(dst);
    if (r != E_OK) { kprintf("cp: %s: %s\r\n", dst, kerr_str(r)); return; }
    r = vfs_write(dst, buf, len);
    if (r != E_OK) { kprintf("cp: %s: %s\r\n", dst, kerr_str(r)); return; }
    kprintf("cp: copied %d bytes -> %s\r\n", len, dst);
}

// --- clear ------------------------------------------------------------------
void cmd_clear(const char* arg) {
    (void)arg;
    uart_puts("\x1b[2J\x1b[H");
}

// --- whoami -----------------------------------------------------------------
void cmd_whoami(const char* arg) {
    (void)arg;
    kprintf("%s (%s)\r\n", g_username,
            g_user_level == USER_LEVEL_ROOT ? "root" : "user");
}

// --- free -------------------------------------------------------------------
void cmd_free(const char* arg) {
    (void)arg;
    uint32_t total = mm_total_bytes();
    uint32_t freeb = mm_free_bytes();
    uint32_t used  = total - freeb;
    uart_puts("            total       used       free\r\n");
    kprintf("Heap:  %10u %10u %10u\r\n", total, used, freeb);
    uint32_t st = 0, su = 0;
    mm_swap_info(&st, &su);
    if (st) kprintf("Swap:  %10u %10u %10u\r\n", st, su, st - su);
}

// --- hexdump ----------------------------------------------------------------
void cmd_hexdump(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("hexdump: hex + ASCII dump of a file\r\nUsage: hexdump <file>\r\n");
        return;
    }
    char path[VFS_MAX_PATH];
    cu_arg1(arg, path, sizeof(path));
    static char buf[VFS_MAX_DATA];
    int len = vfs_read(path, buf, sizeof(buf));
    if (len < 0) { kprintf("hexdump: %s: %s\r\n", path, kerr_str(len)); return; }
    for (int off = 0; off < len; off += 16) {
        kprintf("%08x  ", (uint32_t)off);
        for (int i = 0; i < 16; i++) {
            if (off + i < len) kprintf("%02x ", (unsigned char)buf[off+i]);
            else               uart_puts("   ");
        }
        uart_puts(" |");
        for (int i = 0; i < 16 && off + i < len; i++) {
            unsigned char c = buf[off+i];
            uart_putc((c >= 0x20 && c < 0x7f) ? c : '.');
        }
        uart_puts("|\r\n");
    }
    kprintf("%d bytes\r\n", len);
}

// --- wc ---------------------------------------------------------------------
void cmd_wc(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("wc: count lines, words, bytes\r\nUsage: wc <file>\r\n");
        return;
    }
    char path[VFS_MAX_PATH];
    cu_arg1(arg, path, sizeof(path));
    static char buf[VFS_MAX_DATA];
    int len = vfs_read(path, buf, sizeof(buf));
    if (len < 0) { kprintf("wc: %s: %s\r\n", path, kerr_str(len)); return; }
    int lines = 0, words = 0, inword = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') inword = 0;
        else if (!inword) { inword = 1; words++; }
    }
    kprintf("%6d %6d %6d %s\r\n", lines, words, len, path);
}

// --- find -------------------------------------------------------------------
void cmd_find(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("find: search the filesystem for a name (substring)\r\n"
                  "Usage: find <name>\r\n");
        return;
    }
    char q[VFS_MAX_NAME];
    cu_arg1(arg, q, sizeof(q));
    int n = vfs_node_count();
    int hits = 0;
    char path[VFS_MAX_PATH];
    for (int i = 1; i < n; i++) {   // skip root (0)
        const vfs_node_t* node = vfs_node_get(i);
        if (!node) continue;
        if (cu_contains(node->name, q)) {
            vfs_path_of(i, path, sizeof(path));
            kprintf("%s%s\r\n", path, node->type == VFS_TYPE_DIR ? "/" : "");
            hits++;
        }
    }
    if (!hits) kprintf("find: no match for '%s'\r\n", q);
}

// --- calc -------------------------------------------------------------------
// Integer recursive-descent evaluator: + - * / and parentheses, standard
// precedence. cu_err flags divide-by-zero / parse errors.
static const char* g_calc_p;
static int g_calc_err;
static int calc_expr();

static void calc_skip() { while (*g_calc_p == ' ' || *g_calc_p == '\t') g_calc_p++; }

static int calc_factor() {
    calc_skip();
    if (*g_calc_p == '(') {
        g_calc_p++;
        int v = calc_expr();
        calc_skip();
        if (*g_calc_p == ')') g_calc_p++; else g_calc_err = 1;
        return v;
    }
    if (*g_calc_p == '-') { g_calc_p++; return -calc_factor(); }
    if (*g_calc_p >= '0' && *g_calc_p <= '9') {
        const char* end;
        int v = cu_atoi(g_calc_p, &end);
        g_calc_p = end;
        return v;
    }
    g_calc_err = 1;
    return 0;
}

static int calc_term() {
    int v = calc_factor();
    while (1) {
        calc_skip();
        char op = *g_calc_p;
        if (op != '*' && op != '/') break;
        g_calc_p++;
        int r = calc_factor();
        if (op == '*') v *= r;
        else { if (r == 0) { g_calc_err = 1; return 0; } v /= r; }
    }
    return v;
}

static int calc_expr() {
    int v = calc_term();
    while (1) {
        calc_skip();
        char op = *g_calc_p;
        if (op != '+' && op != '-') break;
        g_calc_p++;
        int r = calc_term();
        if (op == '+') v += r; else v -= r;
    }
    return v;
}

void cmd_calc(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("calc: integer calculator (+ - * / and parentheses)\r\n"
                  "Usage: calc <expression>\r\n");
        return;
    }
    g_calc_p = arg;
    g_calc_err = 0;
    int v = calc_expr();
    calc_skip();
    if (g_calc_err || *g_calc_p) uart_puts("calc: invalid expression\r\n");
    else kprintf("%d\r\n", v);
}

// --- sleep ------------------------------------------------------------------
void cmd_sleep(const char* arg) {
    if (!arg[0] || cu_is_help(arg)) {
        uart_puts("sleep: busy-wait delay\r\nUsage: sleep <milliseconds>\r\n");
        return;
    }
    int ms = cu_atoi(arg, 0);
    if (ms <= 0) return;
    if (ms > 60000) ms = 60000;   // cap (32-bit cycle counter)
    uint32_t need = (uint32_t)ms * 1000u * CYCLES_PER_US;  // ms -> us -> cycles
    uint32_t last = get_mcycle(), acc = 0;
    while (acc < need) {
        uint32_t now = get_mcycle();
        acc += now - last;   // unsigned diff tolerates counter wrap
        last = now;
    }
}

// --- date -------------------------------------------------------------------
void cmd_date(const char* arg) {
    (void)arg;
    uint32_t us  = get_mcycle() / CYCLES_PER_US;
    uint32_t sec = us / 1000000u;
    kprintf("%02u:%02u:%02u  (%u s since boot; no RTC)\r\n",
            (sec / 3600u) % 24u, (sec / 60u) % 60u, sec % 60u, sec);
}
