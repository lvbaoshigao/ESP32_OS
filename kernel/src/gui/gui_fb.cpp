#include "gui.h"
#include "kernel.h"

// === Framebuffer (double-buffer) ===
// ponytail: no GDMA yet — add when SPI LCD hardware is connected.
// For now, framebuffer is purely in-memory for software rendering.

static framebuffer_t g_fb;

int fb_init(uint16_t w, uint16_t h) {
    if (g_fb.active) return -1;
    size_t bytes = (size_t)w * h * GUI_FB_BPP;
    int single = 0;

    if (bytes * 2 > mm_free_bytes()) {
        // fix: try single buffer if double buffer doesn't fit (512KB SRAM limit)
        if (bytes > mm_free_bytes())
            return -2;
        single = 1;
    }

    size_t alloc_size = single ? bytes : bytes * 2;
    uint16_t *buf = (uint16_t*)mm_alloc(alloc_size);
    if (!buf) return -3;

    g_fb.base   = buf;                         // fix: save original allocation pointer
    g_fb.front  = buf;
    g_fb.back   = single ? buf : buf + (w * h);
    g_fb.width  = w;
    g_fb.height = h;
    g_fb.bpp    = GUI_FB_BPP;
    g_fb.ready  = 1;
    g_fb.active = 1;

    // Clear buffer(s)
    k_memset(g_fb.front, 0, bytes);
    if (!single) k_memset(g_fb.back, 0, bytes);
    return 0;
}

void fb_swap(void) {
    if (!g_fb.active) return;
    uint16_t *tmp = g_fb.front;
    g_fb.front = g_fb.back;
    g_fb.back = tmp;
    g_fb.ready = 1;
}

uint16_t* fb_get_draw_buffer(void) {
    return g_fb.active ? g_fb.back : 0;
}

void fb_deinit(void) {
    if (!g_fb.active) return;
    // fix: free base (original alloc ptr), not front (may point to middle after swap)
    if (g_fb.base) mm_free(g_fb.base);
    k_memset(&g_fb, 0, sizeof(g_fb));
}

framebuffer_t* fb_get(void) { return &g_fb; }