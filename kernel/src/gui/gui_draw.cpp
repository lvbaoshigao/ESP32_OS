#include "gui.h"
#include "kernel.h"

// === Drawing primitives — software-rendered on framebuffer ===

// fix: 32-bit per-pixel fill (two RGB565 pixels at once)
static inline void memset16(uint16_t *dst, uint16_t val, uint32_t count) {
    uint32_t v32 = ((uint32_t)val << 16) | val;
    uint32_t i = 0;
    for (; i + 1 < count; i += 2) {
        *(uint32_t*)(dst + i) = v32;
    }
    if (i < count) dst[i] = val;
}

static inline void pset(uint16_t x, uint16_t y, uint16_t color) {
    framebuffer_t *f = fb_get();
    if (!f || !f->back) return;
    if (x >= f->width || y >= f->height) return;
    f->back[y * f->width + x] = color;
}

void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    pset(x, y, color);
}

void draw_fill(uint16_t color) {
    framebuffer_t *f = fb_get();
    if (!f || !f->back) return;
    uint32_t count = (uint32_t)f->width * f->height;
    for (uint32_t i = 0; i < count; i++)
        f->back[i] = color;
}

void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t fill) {
    framebuffer_t *f = fb_get();
    if (!f || !f->back) return;
    // Clip
    if (x >= f->width || y >= f->height) return;
    if (x + w > f->width)  w = f->width - x;
    if (y + h > f->height) h = f->height - y;

    if (fill) {
        // fix: use 32-bit writes (two pixels at once) via memset16
        for (uint16_t row = 0; row < h; row++) {
            uint16_t *line = f->back + (y + row) * f->width + x;
            memset16(line, color, w);
        }
    } else {
        // Top/bottom
        for (uint16_t col = 0; col < w; col++) {
            pset(x + col, y, color);
            pset(x + col, y + h - 1, color);
        }
        // Left/right
        for (uint16_t row = 1; row < h - 1; row++) {
            pset(x, y + row, color);
            pset(x + w - 1, y + row, color);
        }
    }
}

void draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    // Bresenham line algorithm
    int16_t dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int16_t dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int16_t sx = x1 < x2 ? 1 : -1;
    int16_t sy = y1 < y2 ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        pset((uint16_t)x1, (uint16_t)y1, color);
        if (x1 == x2 && y1 == y2) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

// fix: inline horizontal line fill for circle (replaces incorrect draw_line call)
static inline void draw_hline(uint16_t x1, uint16_t x2, uint16_t y, uint16_t color) {
    framebuffer_t *f = fb_get();
    if (!f || !f->back || y >= f->height) return;
    if (x1 > x2) { uint16_t t = x1; x1 = x2; x2 = t; }
    if (x2 >= f->width) x2 = f->width - 1;
    if (x1 >= f->width) return;
    uint16_t *line = f->back + y * f->width;
    memset16(line + x1, color, x2 - x1 + 1);
}

void draw_circle(int16_t cx, int16_t cy, uint16_t r, uint16_t color, uint8_t fill) {
    // Bresenham circle
    int16_t x = 0, y = (int16_t)r;
    int16_t d = 3 - 2 * (int16_t)r;

    while (x <= y) {
        if (fill) {
            // fix: use draw_hline (direct pixel fill) instead of draw_line (Bresenham)
            draw_hline((uint16_t)(cx - x), (uint16_t)(cx + x), (uint16_t)(cy + y), color);
            draw_hline((uint16_t)(cx - x), (uint16_t)(cx + x), (uint16_t)(cy - y), color);
            draw_hline((uint16_t)(cx - y), (uint16_t)(cx + y), (uint16_t)(cy + x), color);
            draw_hline((uint16_t)(cx - y), (uint16_t)(cx + y), (uint16_t)(cy - x), color);
        } else {
            pset(cx + x, cy + y, color);
            pset(cx - x, cy + y, color);
            pset(cx + x, cy - y, color);
            pset(cx - x, cy - y, color);
            pset(cx + y, cy + x, color);
            pset(cx - y, cy + x, color);
            pset(cx + y, cy - x, color);
            pset(cx - y, cy - x, color);
        }
        x++;
        if (d < 0) d += 4 * x + 6;
        else { y--; d += 4 * (x - y) + 10; }
    }
}

void draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data) {
    framebuffer_t *f = fb_get();
    if (!f || !f->back || !data) return;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint16_t px = x + col;
            uint16_t py = y + row;
            if (px < f->width && py < f->height)
                f->back[py * f->width + px] = data[row * w + col];
        }
    }
}