#include "gui.h"
#include "kernel.h"

// === Window manager ===

static window_t g_windows[GUI_WIN_MAX];
static int g_win_cnt = 0;
static uint16_t g_next_id = 1;

// fix: dirty flag — skip full redraw when nothing changed
static volatile uint8_t g_need_render = 1;
static void gui_mark_dirty(void) { g_need_render = 1; }

// Event queue (ring buffer)
static event_t g_ev_queue[GUI_EV_Q];
static int g_ev_head = 0, g_ev_tail = 0, g_ev_cnt = 0;

int ev_push(event_t *ev) {
    if (g_ev_cnt >= GUI_EV_Q) return -1;
    g_ev_queue[g_ev_head] = *ev;
    g_ev_head = (g_ev_head + 1) % GUI_EV_Q;
    g_ev_cnt++;
    return 0;
}

int ev_pop(event_t *ev) {
    if (g_ev_cnt <= 0) return -1;
    *ev = g_ev_queue[g_ev_tail];
    g_ev_tail = (g_ev_tail + 1) % GUI_EV_Q;
    g_ev_cnt--;
    return 0;
}

int win_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *title) {
    if (g_win_cnt >= GUI_WIN_MAX) return -1;
    window_t *win = &g_windows[g_win_cnt];
    k_memset(win, 0, sizeof(window_t));
    win->id = g_next_id++;
    win->x = (int16_t)x;
    win->y = (int16_t)y;
    win->width = w;
    win->height = h;
    win->z_order = (uint8_t)g_win_cnt;
    win->visible = 1;
    win->active = 1;
    if (title) k_strncpy(win->title, title, sizeof(win->title) - 1);
    g_win_cnt++;
    gui_mark_dirty();
    return win->id;
}

int win_destroy(uint16_t id) {
    int idx = -1;
    for (int i = 0; i < g_win_cnt; i++) {
        if (g_windows[i].id == id) { idx = i; break; }
    }
    if (idx < 0) return -1;
    // Free per-window buffer if allocated
    if (g_windows[idx].buffer) mm_free(g_windows[idx].buffer);
    // fix: free widget private data allocated by button/label_create
    if (g_windows[idx].priv) { mm_free(g_windows[idx].priv); g_windows[idx].priv = 0; }
    // Shift remaining windows
    for (int i = idx; i < g_win_cnt - 1; i++)
        g_windows[i] = g_windows[i + 1];
    g_win_cnt--;
    // fix: reassign z_order after shift (issue #12)
    for (int i = 0; i < g_win_cnt; i++)
        g_windows[i].z_order = (uint8_t)i;
    gui_mark_dirty();
    return 0;
}

int win_move(uint16_t id, int16_t nx, int16_t ny) {
    window_t *win = win_get(id);
    if (!win) return -1;
    win->x = nx;
    win->y = ny;
    gui_mark_dirty();
    return 0;
}

int win_resize(uint16_t id, uint16_t nw, uint16_t nh) {
    window_t *win = win_get(id);
    if (!win) return -1;
    win->width = nw;
    win->height = nh;
    gui_mark_dirty();
    return 0;
}

int win_focus(uint16_t id) {
    for (int i = 0; i < g_win_cnt; i++) {
        g_windows[i].focused = (g_windows[i].id == id) ? 1 : 0;
    }
    gui_mark_dirty();
    return 0;
}

int win_hide(uint16_t id) {
    window_t *win = win_get(id);
    if (!win) return -1;
    win->visible = 0;
    gui_mark_dirty();
    return 0;
}

int win_show(uint16_t id) {
    window_t *win = win_get(id);
    if (!win) return -1;
    win->visible = 1;
    gui_mark_dirty();
    return 0;
}

int win_bring_to_top(uint16_t id) {
    int idx = -1;
    for (int i = 0; i < g_win_cnt; i++) {
        if (g_windows[i].id == id) { idx = i; break; }
    }
    if (idx < 0) return -1;
    window_t tmp = g_windows[idx];
    for (int i = idx; i < g_win_cnt - 1; i++)
        g_windows[i] = g_windows[i + 1];
    g_windows[g_win_cnt - 1] = tmp;
    // Reassign z_order
    for (int i = 0; i < g_win_cnt; i++)
        g_windows[i].z_order = (uint8_t)i;
    gui_mark_dirty();
    return 0;
}

window_t* win_get(uint16_t id) {
    for (int i = 0; i < g_win_cnt; i++) {
        if (g_windows[i].id == id) return &g_windows[i];
    }
    return 0;
}

int win_count(void) { return g_win_cnt; }

window_t* win_get_by_index(int idx) {
    if (idx < 0 || idx >= g_win_cnt) return 0;
    return &g_windows[idx];
}

void win_dispatch_event(event_t *ev) {
    if (!ev) return;
    // Find focused window
    window_t *target = 0;
    for (int i = 0; i < g_win_cnt; i++) {
        if (g_windows[i].focused && g_windows[i].visible) {
            target = &g_windows[i];
            break;
        }
    }
    // If no focused window, find by coordinates (topmost first)
    if (!target) {
        for (int i = g_win_cnt - 1; i >= 0; i--) {
            window_t *w = &g_windows[i];
            if (!w->visible) continue;
            // fix: cast both sides to int16_t to avoid uint16_t overflow when w->x < 0
            if ((int16_t)ev->x >= w->x && (int16_t)ev->x < (int16_t)(w->x + w->width) &&
                (int16_t)ev->y >= w->y && (int16_t)ev->y < (int16_t)(w->y + w->height)) {
                target = w;
                break;
            }
        }
    }
    if (target && target->on_event) {
        target->on_event(target, ev);
        gui_mark_dirty();   // fix: event may change visual state (e.g. button pressed)
    }
}

// === Render all windows to framebuffer ===
void win_render_all(void) {
    // fix: skip full redraw if nothing changed
    if (!g_need_render) return;
    g_need_render = 0;

    framebuffer_t *f = fb_get();
    if (!f || !f->back) return;

    // Draw desktop background
    draw_fill(GUI_DKBLUE);

    // Draw windows in Z-order
    for (int i = 0; i < g_win_cnt; i++) {
        window_t *w = &g_windows[i];
        if (!w->visible) continue;

        // Draw window background
        draw_rect((uint16_t)w->x, (uint16_t)w->y, w->width, w->height, GUI_GRAY, 1);
        // Title bar
        draw_rect((uint16_t)w->x, (uint16_t)w->y, w->width, 16, w->focused ? GUI_BLUE : GUI_BLACK, 1);
        // Title text
        draw_text((uint16_t)(w->x + 2), (uint16_t)(w->y + 2), w->title, GUI_WHITE);

        // Call window draw callback
        if (w->draw) w->draw(w);
    }
}