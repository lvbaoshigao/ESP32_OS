# ESP32-C6 Bare-Metal GUI Subsystem — Development Task Specification

## Overview

Implement a lightweight, bare-metal GUI subsystem for the ESP32-C6 (RV32IMAC) running the ESPOS kernel. No ESP-IDF, no LVGL, no emWin, no TouchGFX — all code directly accesses hardware registers.

### Platform

| Parameter | Value |
|-----------|-------|
| MCU | ESP32-C6 (RISC-V RV32IMAC, single core) |
| SRAM | 512KB HP SRAM + 16KB LP SRAM |
| Display | SPI LCD (ILI9341/ST7789/ST7735/GC9A01) |
| Touch | I2C (FT5x06/GT911/CST8XX) |
| Flash | 8MB embedded |

### Principles

- Register-level drivers — direct hardware access, no HAL
- Zero-copy via GDMA where possible
- Memory-conscious — 512KB SRAM is limited
- Modular compilation — enable/disable via config macro

---

## Code Structure

```
kernel/src/gui/
├── gui_core.cpp        # Init, event loop, main control
├── gui_draw.cpp        # Drawing primitives
├── gui_font.cpp        # Bitmap font rendering
├── gui_window.cpp      # Window manager
├── gui_fb.cpp          # Framebuffer (double-buffer + DMA)
├── gui_lcd_spi.cpp     # SPI LCD driver
├── gui_touch_i2c.cpp   # I2C touch driver
├── gui_widget.cpp      # Widget library (button, label)
└── gui_cmd.cpp         # gui command family (shell integration)

kernel/include/gui.h    # Public API header
```

---

## Module 1: Framebuffer (`gui_fb.cpp`)

Double-buffer mechanism to avoid tearing.

```c
struct framebuffer {
    uint16_t *front;           // front buffer (LCD scanning)
    uint16_t *back;            // back buffer (CPU drawing)
    uint16_t width, height;
    uint8_t bpp;               // bytes per pixel (2 for RGB565)
    volatile uint8_t ready;    // DMA transfer complete flag
};

int  fb_init(uint16_t w, uint16_t h);
void fb_swap(void);
uint16_t* fb_get_draw_buffer(void);
void fb_deinit(void);
```

- Allocate via `mm_alloc`
- 320×240 RGB565 double-buffer ≈ 300KB

---

## Module 2: SPI LCD Driver (`gui_lcd_spi.cpp`)

Direct register access to ESP32-C6 SPI2 peripheral.

```c
int  lcd_init(void);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_data(uint8_t *data, size_t len);
void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void lcd_write_pixels(uint16_t *pixels, size_t count);
```

- GPIO: DC, CS, RST, SCLK, MOSI via direct register access
- SPI: 10-40MHz, mode 0, MSB-first, GDMA enabled
- Panel init sequence for ILI9341/ST7789/ST7735/GC9A01

---

## Module 3: Touch Driver (`gui_touch_i2c.cpp`)

I2C touch controller (FT5x06, GT911, CST8XX).

```c
struct touch_point { uint16_t x, y; uint8_t pressure, id; };
int  touch_init(void);
int  touch_scan(struct touch_point *pts, int max);
```

- I2C0 at 100-400kHz, direct register access
- 7-bit I2C addressing
- Multi-touch support

---

## Module 4: Drawing Engine (`gui_draw.cpp`)

Software-rendered drawing primitives on framebuffer.

```c
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t fill);
void draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color, uint8_t fill);
void draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
```

- Color: RGB565 `#define RGB565(r,g,b) ((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))`
- Clip before draw, `k_memset` for fills, Bresenham for lines/circles

---

## Module 5: Font Rendering (`gui_font.cpp`)

8×16 monospace bitmap font for ASCII 0x20-0x7E.

```c
void draw_char(uint16_t x, uint16_t y, char c, uint16_t color);
void draw_text(uint16_t x, uint16_t y, const char *str, uint16_t color);
```

- Font data in read-only flash (16 bytes per char, 1bpp)

---

## Module 6: Window Manager (`gui_window.cpp`)

```c
struct window {
    uint16_t id, x, y, width, height;
    uint8_t z_order, visible, focused;
    uint16_t *buffer;
    void (*draw)(struct window *);
    void (*on_event)(struct window *, struct event *);
};

int win_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *title);
int win_destroy(uint16_t id);
int win_move(uint16_t id, uint16_t nx, uint16_t ny);
int win_focus(uint16_t id);
int win_hide(uint16_t id);
int win_show(uint16_t id);
int win_bring_to_top(uint16_t id);
```

Event types: `TOUCH_DOWN/UP/MOVE`, `KEY_DOWN/UP`, `WINDOW_CLOSE`. Ring buffer event queue, dispatch by coordinates or focus.

---

## Module 7: Widget Library (`gui_widget.cpp`)

```c
// Button with press/release states and click callback
int button_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const char *text, void (*on_click)(void));
// Static text label
int label_create(uint16_t x, uint16_t y, const char *text);
```

---

## Module 8: Shell Integration (`gui_cmd.cpp`)

| Command | Function |
|---------|----------|
| `gui -start` | Init SPI/I2C/LCD/touch/window manager |
| `gui -stop` | Stop GUI, free resources, return to console |
| `gui -exec <path>` | Run a GUI app |
| `gui -list` | List all live windows |
| `gui -kill <id>` | Force-close a window |
| `gui -bench` | Display benchmark (FPS test) |
| `gui -help` | Show help |

---

## Build Config

```c
#define CONFIG_GUI_ENABLE   1    // 0=disable, 1=enable
#define CONFIG_GUI_LCD_W    320  // LCD width
#define CONFIG_GUI_LCD_H    240  // LCD height
```

Add `kernel/src/gui/*.cpp` to `CXX_SRCS` in Makefile.

---

## Development Order

| Phase | Module | Depends On |
|-------|--------|------------|
| P0 | Framebuffer | — |
| P0 | SPI LCD | Framebuffer |
| P0 | Drawing Engine | Framebuffer |
| P0 | Font Rendering | Drawing Engine |
| P1 | Window Manager | Drawing Engine |
| P1 | Shell Integration | Window Manager |
| P2 | Touch Driver | Window Manager |
| P2 | Widget Library | Window Manager |

---

## Restrictions

1. No ESP-IDF — no `esp_lcd`, `esp_lcd_panel` or any other ESP-IDF components
2. No third-party GUI frameworks — no LVGL, emWin, TouchGFX
3. Register-level drivers only — direct hardware access
4. Memory-conscious — adapt buffer sizes to available SRAM
5. Original code only — draw inspiration from open-source, but write everything from scratch
6. `doc.MD` takes precedence in case of conflicts