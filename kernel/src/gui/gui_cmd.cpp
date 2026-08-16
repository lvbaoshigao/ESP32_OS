#include "gui.h"
#include "kernel.h"

// === GUI shell command ===

void cmd_gui(const char *arg) {
    if (!arg[0] || k_strcmp(arg, "-help") == 0) {
        uart_puts("gui: GUI subsystem control\r\n"
                  "Usage:\r\n"
                  "  gui -start          start GUI (init framebuffer + LCD)\r\n"
                  "  gui -stop           stop GUI, free resources\r\n"
                  "  gui -list           list all windows\r\n"
                  "  gui -kill <id>      force-close a window\r\n"
                  "  gui -bench          run display performance test\r\n"
                  "  gui -help           show this help\r\n");
        return;
    }

    char sub[16], name[32];
    const char *p = tok_next(arg, sub, sizeof(sub));

    if (k_strcmp(sub, "-start") == 0) {
        int r = gui_start();
        if (r == 0) {
            uart_puts("GUI started. Enter 'gui -stop' to return to console.\r\n");
        } else if (r == -1) {
            uart_puts("GUI already running.\r\n");
        } else if (r == -2) {
            uart_puts("gui -start: not enough memory for framebuffer.\r\n");
        } else {
            kprintf("gui -start: error %d\r\n", r);
        }

    } else if (k_strcmp(sub, "-stop") == 0) {
        gui_stop();

    } else if (k_strcmp(sub, "-list") == 0) {
        if (!gui_active()) {
            uart_puts("GUI not running.\r\n");
            return;
        }
        int n = win_count();
        if (n == 0) { uart_puts("No windows.\r\n"); return; }
        uart_puts("ID   VIS Z   TITLE\r\n");
        for (int i = 0; i < n; i++) {
            window_t *w = win_get_by_index(i);
            if (!w) continue;
            kprintf("%-4d %-3s %-3d %s\r\n", w->id, w->visible ? "Y" : "N", w->z_order, w->title);
        }

    } else if (k_strcmp(sub, "-kill") == 0) {
        tok_next(p, name, sizeof(name));
        if (!name[0]) { uart_puts("gui -kill: missing window ID\r\n"); return; }
        // Simple atoi
        uint16_t id = 0;
        for (int i = 0; name[i] >= '0' && name[i] <= '9'; i++)
            id = id * 10 + (name[i] - '0');
        if (win_destroy(id) == 0)
            kprintf("gui: window %u destroyed\r\n", id);
        else
            kprintf("gui: window %u not found\r\n", id);

    } else if (k_strcmp(sub, "-bench") == 0) {
        gui_bench();

    } else {
        kprintf("gui: unknown option '%s'. Use 'gui -help'\r\n", sub);
    }
}