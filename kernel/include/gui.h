#ifndef GUI_H
#define GUI_H

#include "kernel.h"

// === GUI subsystem public API ===
// ESP32-C6 bare-metal GUI — no ESP-IDF, no LVGL, direct register access

#ifdef __cplusplus
extern "C" {
#endif

// === Configuration ===
#define GUI_LCD_W    320
#define GUI_LCD_H    240
#define GUI_FB_BPP   2               // RGB565
#define GUI_FB_SIZE  (GUI_LCD_W * GUI_LCD_H * GUI_FB_BPP)  // 150KB
#define GUI_WIN_MAX  16
#define GUI_EV_Q     32

// === Colors (RGB565) ===
#define RGB565(r, g, b)  ((((uint16_t)(r) & 0xF8) << 8) | (((uint16_t)(g) & 0xFC) << 3) | ((uint16_t)(b) >> 3))
#define GUI_BLACK    RGB565(0, 0, 0)
#define GUI_WHITE    RGB565(255, 255, 255)
#define GUI_RED      RGB565(255, 0, 0)
#define GUI_GREEN    RGB565(0, 255, 0)
#define GUI_BLUE     RGB565(0, 0, 255)
#define GUI_CYAN     RGB565(0, 255, 255)
#define GUI_YELLOW   RGB565(255, 255, 0)
#define GUI_GRAY     RGB565(128, 128, 128)
#define GUI_DKBLUE   RGB565(0, 0, 128)

// === Framebuffer ===
typedef struct {
    uint16_t *base;             // original mm_alloc pointer (fix: fb_deinit frees this, not front)
    uint16_t *front;            // front buffer (LCD scanning)
    uint16_t *back;             // back buffer (CPU drawing)
    uint16_t width, height;
    uint8_t bpp;
    volatile uint8_t ready;     // DMA transfer complete flag
    volatile uint8_t active;    // GUI is running
} framebuffer_t;

int  fb_init(uint16_t w, uint16_t h);
void fb_swap(void);
uint16_t* fb_get_draw_buffer(void);
void fb_deinit(void);
framebuffer_t* fb_get(void);

// === Drawing primitives ===
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t fill);
void draw_circle(int16_t cx, int16_t cy, uint16_t r, uint16_t color, uint8_t fill);
void draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
void draw_fill(uint16_t color);

// === Font rendering ===
void draw_char(uint16_t x, uint16_t y, char c, uint16_t color);
void draw_text(uint16_t x, uint16_t y, const char *str, uint16_t color);

// === Events ===
typedef enum {
    EV_NONE = 0,
    EV_TOUCH_DOWN,
    EV_TOUCH_UP,
    EV_TOUCH_MOVE,
    EV_KEY_DOWN,
    EV_KEY_UP,
    EV_WIN_CLOSE
} event_type_t;

typedef struct {
    event_type_t type;
    uint16_t x, y;
    uint32_t key;
    uint32_t timestamp;
} event_t;

int  ev_push(event_t *ev);
int  ev_pop(event_t *ev);
void win_render_all(void);

// === Window manager ===
typedef struct window window_t;

typedef void (*win_draw_fn)(window_t *win);
typedef void (*win_event_fn)(window_t *win, event_t *ev);

struct window {
    uint16_t id;
    int16_t x, y;
    uint16_t width, height;
    uint8_t z_order;
    uint8_t visible;
    uint8_t focused;
    uint8_t active;
    char title[24];
    uint16_t *buffer;           // per-window buffer (optional)
    void *priv;                 // widget private data (fix: allocated by button/label_create)
    win_draw_fn draw;
    win_event_fn on_event;
};

int  win_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *title);
int  win_destroy(uint16_t id);
int  win_move(uint16_t id, int16_t nx, int16_t ny);
int  win_resize(uint16_t id, uint16_t nw, uint16_t nh);
int  win_focus(uint16_t id);
int  win_hide(uint16_t id);
int  win_show(uint16_t id);
int  win_bring_to_top(uint16_t id);
window_t* win_get(uint16_t id);
window_t* win_get_by_index(int idx);
int  win_count(void);
void win_dispatch_event(event_t *ev);

// === Widgets ===
int  button_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   const char *text, void (*on_click)(void));
int  label_create(uint16_t x, uint16_t y, const char *text);

// === LCD hardware (stub without physical LCD) ===
int  lcd_init(void);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_data(uint8_t *data, size_t len);
void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void lcd_write_pixels(uint16_t *pixels, size_t count);
void lcd_refresh(void);

// === Touch hardware (stub without physical touch) ===
typedef struct {
    uint16_t x, y;
    uint8_t pressure;
    uint8_t id;
} touch_point_t;

int  touch_init(void);
int  touch_scan(touch_point_t *pts, int max);

// === GUI core ===
int  gui_start(void);
void gui_stop(void);
void gui_task(void);            // called from scheduler task
int  gui_active(void);
void gui_bench(void);

// === Shell command ===
void cmd_gui(const char *arg);

#ifdef __cplusplus
}
#endif

#endif // GUI_H