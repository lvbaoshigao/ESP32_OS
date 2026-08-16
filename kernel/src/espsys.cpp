#include "kernel.h"

// === Driver table ===
driver_t g_drivers[MAX_DRIVERS];
static int g_driver_count = 0;

// === Display mode ===
static int g_display_mode = DISPLAY_SERIAL;

// === Serial driver (real — uses UART0 inherited from boot) ===

static int serial_init() { return 0; }

static int serial_read(void* buf, int len) {
    char* p = (char*)buf;
    for (int i = 0; i < len; i++)
        p[i] = (char)uart_getc();
    return len;
}

static int serial_write(const void* buf, int len) {
    const char* p = (const char*)buf;
    for (int i = 0; i < len; i++)
        uart_putc(p[i]);
    return len;
}

static int serial_ioctl(int cmd, void*) { (void)cmd; return -1; }

static driver_t serial_driver = {
    "serial", serial_init, serial_read, serial_write, serial_ioctl, 0
};

// === Network driver (real — SLIP over UART0) ===
extern driver_t g_net_driver;
extern void net_activate(void);
extern void net_deactivate(void);
extern int net_is_active(void);

// === Video driver (stub) ===

static int video_init() { return -1; }
static int video_read(void*, int) { return -1; }
static int video_write(const void*, int) { return -1; }
static int video_ioctl(int, void*) { return -1; }

static driver_t video_driver = {
    "video", video_init, video_read, video_write, video_ioctl, 0
};

// === Driver framework ===

int driver_register(driver_t* drv) {
    if (g_driver_count >= MAX_DRIVERS) return -1;
    g_drivers[g_driver_count] = *drv;
    int r = drv->init ? drv->init() : 0;
    g_drivers[g_driver_count].active = (r == 0) ? 1 : 0;
    g_driver_count++;
    return r;
}

driver_t* driver_find(const char* name) {
    if (!name) return 0;
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].name && k_strcmp(g_drivers[i].name, name) == 0)
            return &g_drivers[i];
    }
    return 0;
}

void drivers_init() {
    driver_register(&serial_driver);
    driver_register(&g_net_driver);
    driver_register(&video_driver);
    driver_register(&g_wifi_driver);
    driver_register(&g_bt_driver);
}

// === Display mode ===

int display_set_mode(int mode) {
    if (mode < 0 || mode >= DISPLAY_MAX) return -1;
    if (mode == DISPLAY_NETWORK) {
        driver_t* d = driver_find("network");
        if (!d || !d->active) return -1;
        net_activate();
    } else {
        if (net_is_active()) net_deactivate();
    }
    if (mode == DISPLAY_VIDEO) {
        driver_t* d = driver_find("video");
        if (!d || !d->active) return -1;
    }
    g_display_mode = mode;
    return 0;
}

int display_get_mode() { return g_display_mode; }

// === ESPSYS call table ===

static int sys_putc(void* arg) { uart_putc((char)(uint32_t)(long)arg); return 0; }
static int sys_puts(void* arg) { uart_puts((const char*)arg); return 0; }
static int sys_getc(void*) { return uart_getc(); }
static int sys_uptime(void* arg) {
    if (arg) *(uint32_t*)arg = get_mcycle() / CYCLES_PER_US;
    return 0;
}
static int sys_reboot(void*) {
    mailbox_t* mb = (mailbox_t*)MAILBOX_ADDR;
    mb->magic = MB_MAGIC_VALUE;
    mb->status = KERN_STATUS_REBOOT;
    mb->request = BOOT_MODE_NORMAL;
    mb->retry = 0;

    // Flush UART
    while ((REG32(UART0_BASE + UART_STATUS_REG) >> UART_TXFIFO_CNT_SHIFT) & UART_TXFIFO_CNT_MASK)
        ;

    system_reset();
    return 0;
}
static int sys_version(void* arg) {
    if (arg) {
        const char* v = "0.1.0";
        char* dst = (char*)arg;
        while (*v) *dst++ = *v++;
        *dst = 0;
    }
    return 0;
}
static int sys_display_mode(void* arg) {
    if (arg) return display_set_mode(*(int*)arg);
    return display_get_mode();
}
static int sys_driver_io(void*) { return -1; }

typedef int (*sys_handler_t)(void*);
static sys_handler_t espsys_table[SYS_MAX] = {
    sys_putc, sys_puts, sys_getc, sys_uptime,
    sys_reboot, sys_version, sys_display_mode, sys_driver_io
};

void espsys_init() {
    // Table is statically initialized
}

int espsys_call(int nr, void* arg) {
    if (nr < 0 || nr >= SYS_MAX) return -1;
    return espsys_table[nr](arg);
}

// === Uptime: mcycle-based (Direct Boot at 40MHz XTAL, no PLL) ===
// CYCLES_PER_US defined in kernel.h

uint32_t uptime_us() {
    return get_mcycle() / CYCLES_PER_US;
}
