#include "gui.h"
#include "kernel.h"

// === GUI core: init, event loop, bench ===

// (win_render_all declared in gui.h)

int gui_start(void) {
    if (gui_active()) return -1;  // already running

    int r = fb_init(GUI_LCD_W, GUI_LCD_H);
    if (r != 0) return r;

    r = lcd_init();
    if (r != 0) { fb_deinit(); return r; }

    touch_init();

    // Draw desktop background
    draw_fill(GUI_DKBLUE);

    // Create a demo window
    int wid = win_create(20, 20, 280, 200, "Terminal");
    if (wid > 0) {
        window_t *w = win_get(wid);
        if (w) {
            w->draw = 0;  // use default draw
            w->focused = 1;
        }
    }

    // Draw welcome text
    draw_text(40, 40, "ESPOS GUI v0.1", GUI_WHITE);
    draw_text(40, 56, "ESP32-C6 framebuffer", GUI_GREEN);
    draw_text(40, 72, "gui -help for commands", GUI_GRAY);
    draw_text(40, 92, "[No physical LCD]", GUI_RED);
    draw_text(40, 108, "In-memory rendering only", GUI_RED);

    kprintf("[GUI] Started (%dx%d, %dKB buffer)\r\n",
            GUI_LCD_W, GUI_LCD_H, GUI_FB_SIZE * 2 / 1024);
    return 0;
}

void gui_stop(void) {
    if (!gui_active()) return;
    win_destroy(1);  // remove demo window
    fb_deinit();
    kprintf("[GUI] Stopped\r\n");
}

int gui_active(void) {
    framebuffer_t *f = fb_get();
    return f && f->active;
}

// Called from scheduler task — yields when idle
void gui_task(void) {
    touch_point_t pts[2];
    event_t ev;

    while (1) {
        if (!gui_active()) {
            task_yield();  // GUI not active — yield so we don't burn CPU in a busy loop
            continue;
        }

        // Touch scan (stub, returns 0 points)
        int n = touch_scan(pts, 2);

        if (n > 0) {
            // Push touch events
            for (int i = 0; i < n; i++) {
                ev.type = EV_TOUCH_DOWN;
                ev.x = pts[i].x;
                ev.y = pts[i].y;
                ev.timestamp = uptime_us();
                win_dispatch_event(&ev);
            }
        }

        // Process event queue
        event_t qev;
        while (ev_pop(&qev) == 0) {
            win_dispatch_event(&qev);
        }

        // Render all windows
        win_render_all();

        // Swap buffers (simulate vsync)
        fb_swap();

        // ponytail: no actual LCD refresh without hardware
        // lcd_refresh();

        task_yield();  // cooperative: give shell/etc. a turn
    }
}

void gui_bench(void) {
    if (!gui_active()) {
        uart_puts("gui -bench: GUI not started. Run 'gui -start' first.\r\n");
        return;
    }

    uint32_t start = uptime_us();
    uint32_t frames = 0;
    uint32_t deadline = start + 1000000;  // 1 second

    // Draw bouncing rectangles
    int16_t bx = 0, by = 0, bsx = 1, bsy = 1;
    while (uptime_us() < deadline) {
        // Clear
        draw_fill(GUI_DKBLUE);

        // Bouncing rect
        draw_rect((uint16_t)bx, (uint16_t)by, 40, 40, GUI_YELLOW, 1);
        bx += bsx; by += bsy;
        if (bx <= 0 || bx >= (int16_t)(GUI_LCD_W - 40)) bsx = -bsx;
        if (by <= 0 || by >= (int16_t)(GUI_LCD_H - 40)) bsy = -bsy;

        // FPS counter
        char fps_str[16];
        // Simple itoa
        uint32_t f = frames;
        char *p = fps_str + 15;
        *p = '\0';
        do { *--p = '0' + (f % 10); f /= 10; } while (f);
        draw_text(10, 10, "FPS: ", GUI_WHITE);
        draw_text(50, 10, p, GUI_GREEN);

        fb_swap();
        frames++;
    }

    uint32_t elapsed = uptime_us() - start;
    uint32_t fps = (elapsed > 0) ? (frames * 1000000 / elapsed) : 0;
    kprintf("[GUI] Bench: %u frames in %u us = %u FPS\r\n", frames, elapsed, fps);
}