#include "kernel.h"
#include "wifi.h"
#include "gui.h"

// === Forward declarations for task functions ===
static void shell_task();

// === Boot log ring buffer ===
char g_boot_log[BOOT_LOG_SIZE];
int  g_boot_log_pos = 0;

void klog(const char* msg) {
    while (*msg && g_boot_log_pos < BOOT_LOG_SIZE - 1)
        g_boot_log[g_boot_log_pos++] = *msg++;
    g_boot_log[g_boot_log_pos] = '\0';
}

// === Kernel main ===

extern "C" void kernel_main(boot_params_t* bp) {
    g_boot_params = bp;

    if (!bp || bp->magic != BP_MAGIC_VALUE) {
        uart_puts("\r\n[KERN] FATAL: boot_params invalid\r\n");
        while (1) asm volatile("wfi");
    }

    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("  ESPOS Kernel v0.1.0 (ESP32-C6)\r\n");
    uart_puts("========================================\r\n");
    klog("[KERN] Kernel started\r\n");

    static const char* bm[] = {"normal","safe","recovery","factory"};
    static const char* bs[] = {"main_flash","backup","external"};
    kprintf("[KERN] Boot mode: %s, source: %s\r\n",
            bp->boot_mode < 4 ? bm[bp->boot_mode] : "?",
            bp->boot_source < 3 ? bs[bp->boot_source] : "?");
    kprintf("[KERN] Crystal: %uMHz, SRAM: %uKB\r\n",
            bp->hw_crystal / 1000000, bp->hw_sram_size / 1024);
    kprintf("[KERN] Entry: 0x%08x, Load: 0x%08x, Size: %u\r\n",
            bp->kern_entry, bp->kern_load, bp->kern_size);

    // Init memory manager
    mm_init();
    mm_swap_init();
    klog("[KERN] Memory manager ready\r\n");
    kprintf("[KERN] Heap: 0x%08x - 0x%08x (%u bytes), free=%u\r\n",
            (uint32_t)mm_heap_start(), (uint32_t)mm_heap_end(), mm_total_bytes(), mm_free_bytes());

    // Init RAM filesystem
    vfs_init();
    klog("[KERN] VFS ready\r\n");

    // Init environment variables
    env_init();
    klog("[KERN] Environment ready\r\n");

    // Init console
    console_init();
    klog("[KERN] Console ready\r\n");

    // Init subsystems
    espsys_init();
    klog("[KERN] ESPSYS ready\r\n");

    drivers_init();
    klog("[KERN] Drivers initialized\r\n");

    kprintf("[KERN] Drivers: serial=%s, network=%s, video=%s, wifi=%s, bt=%s\r\n",
            driver_find("serial") && driver_find("serial")->active ? "ok" : "fail",
            driver_find("network") && driver_find("network")->active ? "ok" : "n/a",
            driver_find("video") && driver_find("video")->active ? "ok" : "n/a",
            driver_find("wifi") && driver_find("wifi")->active ? "ok" : "n/a",
            driver_find("bluetooth") && driver_find("bluetooth")->active ? "ok" : "n/a");

    kprintf("[KERN] Display mode: serial\r\n");
    kprintf("[KERN] System ready.\r\n\r\n");
    klog("[KERN] System ready\r\n");

    // Wi-Fi/BT status
    uart_puts("[WIFI] AP ready  [BT] ready\r\n");

    // UART RX diagnostic
    {
        uint32_t st = REG32(UART0_BASE + UART_STATUS_REG);
        kprintf("[DIAG] UART: status=0x%08x rx=%u tx=%u\r\n", st, st & 0xFF, (st >> 16) & 0xFF);
        // IO_MUX GPIO17 (U0RXD)=0x60090048 must read 0x300 (MCU_SEL=0|FUN_IE|FUN_PU)
        kprintf("[DIAG] MUX16=0x%08x MUX17=0x%08x\r\n",
                REG32(0x60090044), REG32(0x60090048));
    }

    // Write mailbox: kernel booted OK
    mailbox_t* mb = (mailbox_t*)MAILBOX_ADDR;
    mb->magic = MB_MAGIC_VALUE;
    mb->status = KERN_STATUS_OK;
    mb->request = BOOT_MODE_NORMAL;
    mb->retry = 0;

    // Init scheduler and create shell + GUI render tasks
    task_init();
    task_create("shell", shell_task, 4096);
    task_create("gui", gui_task, 2048);
    klog("[KERN] Scheduler + shell/gui tasks ready\r\n");

    // Hand control to scheduler (this never returns)
    uart_puts("\r\n--- Entering multitasking ---\r\n\r\n");
    task_start();
    // never reached
}

// === Shell task (runs under scheduler) ===
static void shell_task() {
    char line[256];
    while (1) {
        console_prompt();
        console_readline(line, sizeof(line));
        console_dispatch(line);
        task_yield();  // give other tasks a chance
    }
}
