#include "gui.h"
#include "kernel.h"

// === Widget library ===

// Button widget internals (fix: no longer embeds window_t — allocated via mm_alloc, stored in win->priv)
typedef struct {
    const char *text;           // pointer into win->title (copied by k_strncpy)
    uint16_t bg_color;
    uint16_t text_color;
    uint8_t pressed;
    void (*on_click)(void);
} button_priv_t;

static void button_draw(window_t *win) {
    button_priv_t *btn = (button_priv_t*)win->priv;
    if (!btn) return;
    uint16_t bg = btn->pressed ? GUI_CYAN : btn->bg_color;
    draw_rect((uint16_t)win->x, (uint16_t)win->y, win->width, win->height, bg, 1);
    if (btn->pressed) {
        draw_rect((uint16_t)win->x, (uint16_t)win->y, win->width, win->height, GUI_WHITE, 0);
    }
    // Centered text
    int tx = win->x + (win->width - (int)k_strlen(btn->text) * 8) / 2;
    int ty = win->y + (win->height - 16) / 2;
    draw_text((uint16_t)tx, (uint16_t)ty, btn->text, btn->text_color);
}

static void button_event(window_t *win, event_t *ev) {
    button_priv_t *btn = (button_priv_t*)win->priv;
    if (!btn) return;
    if (ev->type == EV_TOUCH_DOWN) {
        btn->pressed = 1;
        if (btn->on_click) btn->on_click();
    } else if (ev->type == EV_TOUCH_UP) {
        btn->pressed = 0;
    }
}

int button_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const char *text, void (*on_click)(void)) {
    int id = win_create(x, y, w, h, "");
    if (id < 0) return -1;
    window_t *win = win_get(id);
    if (!win) return -1;

    // fix: allocate button_priv_t, store in win->priv
    button_priv_t *btn = (button_priv_t*)mm_alloc(sizeof(button_priv_t));
    if (!btn) { win_destroy(id); return -1; }
    btn->text = win->title;             // render text from win->title
    btn->bg_color = GUI_GRAY;
    btn->text_color = GUI_WHITE;
    btn->pressed = 0;
    btn->on_click = on_click;

    // fix: store text in win->title
    if (text) k_strncpy(win->title, text, sizeof(win->title) - 1);

    win->draw = button_draw;
    win->on_event = button_event;
    win->priv = btn;
    return id;
}

// Label widget (fix: same pattern — priv data in win->priv, text in win->title)
typedef struct {
    const char *text;
    uint16_t text_color;
} label_priv_t;

static void label_draw(window_t *win) {
    label_priv_t *lp = (label_priv_t*)win->priv;
    // Background is transparent — rely on parent or desktop
    draw_text((uint16_t)(win->x + 2), (uint16_t)(win->y + 2),
              lp ? lp->text : win->title, GUI_WHITE);
}

int label_create(uint16_t x, uint16_t y, const char *text) {
    int id = win_create(x, y, 8 * k_strlen(text) + 4, 20, "");
    if (id < 0) return -1;
    window_t *win = win_get(id);
    if (!win) return -1;

    label_priv_t *lp = (label_priv_t*)mm_alloc(sizeof(label_priv_t));
    if (!lp) { win_destroy(id); return -1; }
    lp->text = win->title;
    lp->text_color = GUI_WHITE;
    if (text) k_strncpy(win->title, text, sizeof(win->title) - 1);

    win->draw = label_draw;
    win->priv = lp;
    return id;
}