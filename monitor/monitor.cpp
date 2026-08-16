// ESPOS Serial Monitor — C++/GTK3 version
// g++ -std=c++17 monitor.cpp -o monitor `pkg-config --cflags --libs gtk+-3.0` -lpthread

#include <gtk/gtk.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <pango/pango.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <regex>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/ioctl.h>

// ===================================================================
// i18n
// ===================================================================

static bool is_zh() {
    const char* lang = getenv("LANG");
    return lang && (strncmp(lang, "zh", 2) == 0);
}
#define _(zh, en) (is_zh() ? (zh) : (en))

// ===================================================================
// ANSI terminal emulator
// ===================================================================

struct AnsiCell {
    char ch;
    signed char fg;   // -1 = default, else palette index 0..15
    signed char bg;   // -1 = default, else palette index 0..15
    bool bold;
};

class AnsiTerminal {
public:
    static const int ROWS = 24;
    static const int COLS = 80;

    AnsiCell grid[ROWS][COLS];
    int cr = 0, cc = 0;
    signed char fg = -1, bg = -1;
    bool bold = false;
    bool dirty = true;
    int saved_cr = 0, saved_cc = 0;

    AnsiTerminal() { clear(); }

    void clear() {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                grid[r][c] = { ' ', -1, -1, false };
        cr = cc = 0; fg = bg = -1; bold = false; dirty = true;
    }

    void feed(const std::string& text) {
        for (size_t i = 0; i < text.size(); i++) {
            unsigned char c = text[i];
            if (c == '\x1b' && i + 1 < text.size() && text[i+1] == '[') {
                i = parse_csi(text, i + 2);
            } else if (c == '\n') {
                if (++cr >= ROWS) cr = ROWS - 1;
            } else if (c == '\r') {
                cc = 0;
            } else if (c == '\b') {
                if (cc > 0) cc--;
            } else if (c >= ' ') {
                if (cr < ROWS && cc < COLS) {
                    signed char efg = fg;
                    if (bold && efg >= 0 && efg < 8) efg += 8;  // bold brightens fg
                    grid[cr][cc] = { (char)c, efg, bg, bold };
                    if (++cc >= COLS) { cc = 0; if (++cr >= ROWS) cr = ROWS - 1; }
                }
            }
        }
        dirty = true;
    }

private:
    size_t parse_csi(const std::string& s, size_t i) {
        std::string params;
        while (i < s.size() && strchr("ABCDEFGHJKSTfmnsu", s[i]) == nullptr)
            params += s[i++];
        if (i >= s.size()) return i;
        char cmd = s[i];
        std::vector<int> vals;
        if (!params.empty()) {
            size_t pos = 0;
            while (pos < params.size()) {
                size_t end = params.find(';', pos);
                int v = atoi(params.substr(pos, end - pos).c_str());
                vals.push_back(v);
                if (end == std::string::npos) break;
                pos = end + 1;
            }
        } else {
            vals.push_back(0);
        }
        exec(cmd, vals);
        return i;   // index of the command char; feed()'s loop i++ steps past it
    }

    void exec(char cmd, const std::vector<int>& vals) {
        if (cmd == 'H' || cmd == 'f') {
            cr = std::max(0, std::min(ROWS - 1, (vals.empty() ? 1 : (vals[0] ? vals[0] : 1)) - 1));
            int cv = (vals.size() < 2 ? 1 : (vals[1] ? vals[1] : 1));
            cc = std::max(0, std::min(COLS - 1, cv - 1));
        } else if (cmd == 'J' && !vals.empty() && vals[0] == 2) {
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++)
                    grid[r][c] = { ' ', -1, -1, false };
            cr = cc = 0;
        } else if (cmd == 'K') {
            for (int c = cc; c < COLS; c++)
                grid[cr][c] = { ' ', -1, -1, false };
        } else if (cmd == 'm') {
            for (int v : vals) {
                if (v == 0)                    { fg = bg = -1; bold = false; }
                else if (v == 1)               { bold = true; }
                else if (v == 22)              { bold = false; }
                else if (v == 7)               { signed char t = fg; fg = bg; bg = t; } // reverse
                else if (v >= 30 && v <= 37)   fg = (signed char)(v - 30);
                else if (v == 39)              fg = -1;
                else if (v >= 90 && v <= 97)   fg = (signed char)(v - 90 + 8);
                else if (v >= 40 && v <= 47)   bg = (signed char)(v - 40);
                else if (v == 49)              bg = -1;
                else if (v >= 100 && v <= 107) bg = (signed char)(v - 100 + 8);
            }
        } else if (cmd == 's') {
            saved_cr = cr; saved_cc = cc;
        } else if (cmd == 'u') {
            cr = saved_cr; cc = saved_cc;
        }
    }
};

// ===================================================================
// Serial port helpers
// ===================================================================

static int serial_open(const std::string& port, int baud) {
    int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 1;  // 100ms

    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:     speed = B115200;
    }
    cfsetospeed(&tio, speed);
    cfsetispeed(&tio, speed);

    if (tcsetattr(fd, TCSANOW, &tio) < 0) { close(fd); return -1; }

    // Clear DTR/RTS to avoid reset on connect
    int flags = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &flags);

    // Switch back to blocking
    fcntl(fd, F_SETFL, 0);
    return fd;
}

static std::vector<std::string> list_ports() {
    std::vector<std::string> ports;
    DIR* dir = opendir("/dev");
    if (!dir) return ports;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0)
            ports.push_back("/dev/" + name);
    }
    closedir(dir);
    std::sort(ports.begin(), ports.end());
    return ports;
}

// ===================================================================
// Monitor application state
// ===================================================================

struct App {
    // Widgets
    GtkWidget* window;
    GtkWidget* tv;
    GtkTextBuffer* buf;
    GtkWidget* stack;      // switches between text terminal and desktop canvas
    GtkWidget* canvas;     // Cairo-drawn desktop (ANSI grid)
    GtkWidget* port_combo;
    GtkWidget* baud_combo;
    GtkWidget* connect_btn;
    GtkWidget* status_label;
    GtkWidget* rxtx_label;
    GtkWidget* entry;
    GtkWidget* fs_label;
    GtkWidget* info_labels[10];
    GtkWidget* pause_btn;
    GtkWidget* ts_btn;
    GtkWidget* reconnect_btn;

    // State
    int serial_fd = -1;
    std::atomic<bool> running{false};
    std::thread* reader_thread = nullptr;
    bool auto_reconnect = true;
    bool paused = false;
    bool show_timestamps = true;
    bool hex_mode = false;
    bool debug_mode = false;
    int font_pt = 13;
    std::atomic<long> bytes_rx{0};
    std::atomic<long> bytes_tx{0};
    AnsiTerminal ansi;
    bool ansi_mode = false;
    int hover_r = -1, hover_c = -1;   // last mouse cell sent to the ESP (dedup)
    int pend_r = -1, pend_c = -1;     // throttled pending hover cell
    bool hover_scheduled = false;     // a coalesced hover flush is queued

    // Info data
    std::string info_keys[10] = {"status","boot_mode","boot_src","crystal","sram",
                                  "heap","kernel_ver","drivers","entry","uptime"};
    std::string info_vals[10];
    std::string info_labels_str[10] = {
        _("状态","Status"), _("启动模式","Boot Mode"), _("启动源","Boot Source"),
        _("晶振","Crystal"), _("内存","SRAM"), _("堆","Heap"),
        _("内核版本","Kernel"), _("驱动","Drivers"), _("入口地址","Entry"),
        _("运行时间","Uptime")
    };

    std::string current_port;
    int current_baud = 115200;

    // Command history (Up/Down in the input entry)
    std::vector<std::string> history;
    int hist_idx = 0;

    // Tags
    GtkTextTag* tags[20];
    int tag_count = 0;

    // Timer IDs
    guint no_data_timer = 0;
    guint query_timer = 0;
    guint reconnect_timer = 0;
};

static App app;

// ===================================================================
// Forward declarations
// ===================================================================

static void append_line(const std::string& text, const std::string& tag_name);
static void debug_msg(const std::string& msg);
static void update_rxtx();
static void set_status(const std::string& text, const std::string& color);
static void do_connect(const std::string& port, int baud);
static void do_disconnect();
static void process_line(const std::string& text);
static void update_info(int idx, const std::string& value);
static void start_auto_connect();
static void stop_auto_connect();

// ===================================================================
// Desktop canvas (Cairo) — renders the ANSI grid as a real graphical
// desktop instead of colored text in the terminal.
// ===================================================================

static const int CELL_W = 9;
static const int CELL_H = 18;
static const double FONT_PX = 14.0;

// 16-colour palette (0-7 normal, 8-15 bright) as RGB.
static const double PALETTE[16][3] = {
    {0.11,0.11,0.13}, {0.80,0.25,0.25}, {0.25,0.72,0.35}, {0.82,0.72,0.22},
    {0.20,0.42,0.86}, {0.80,0.32,0.80}, {0.22,0.72,0.82}, {0.82,0.82,0.85},
    {0.45,0.45,0.50}, {1.00,0.45,0.45}, {0.45,0.92,0.55}, {1.00,0.92,0.35},
    {0.42,0.62,1.00}, {1.00,0.55,1.00}, {0.45,0.92,1.00}, {1.00,1.00,1.00},
};
static const double DEF_FG[3] = {0.87, 0.87, 0.87};
static const double DEF_BG[3] = {0.05, 0.05, 0.05};

static void col_of(signed char idx, const double* def, double* out) {
    const double* c = (idx >= 0 && idx < 16) ? PALETTE[idx] : def;
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
}

static gboolean on_canvas_draw(GtkWidget*, cairo_t* cr, gpointer) {
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_PX);
    // Overall background
    cairo_set_source_rgb(cr, DEF_BG[0], DEF_BG[1], DEF_BG[2]);
    cairo_paint(cr);
    for (int r = 0; r < AnsiTerminal::ROWS; r++) {
        for (int c = 0; c < AnsiTerminal::COLS; c++) {
            const AnsiCell& cell = app.ansi.grid[r][c];
            double x = c * CELL_W, y = r * CELL_H;
            double rgb[3];
            // Background cell
            if (cell.bg >= 0) {
                col_of(cell.bg, DEF_BG, rgb);
                cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
                cairo_rectangle(cr, x, y, CELL_W, CELL_H);
                cairo_fill(cr);
            }
            // Glyph
            if (cell.ch > ' ') {
                col_of(cell.fg, DEF_FG, rgb);
                cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
                cairo_move_to(cr, x + 1, y + CELL_H - 4);
                char s[2] = { cell.ch, 0 };
                cairo_show_text(cr, s);
            }
        }
    }
    return FALSE;
}

// --- Mouse forwarding -------------------------------------------------------
// Translate canvas mouse events into the kernel's custom CSI so the desktop is
// clickable: ESC [ <event> ; <row> ; <col> M  (event 0=hover, 1=click; 1-based).
static void desktop_send_mouse(int event, int row, int col) {
    if (app.serial_fd < 0) return;
    char seq[32];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d;%d;%dM", event, row, col);
    if (write(app.serial_fd, seq, n) == n) {
        app.bytes_tx += n;
        update_rxtx();
    }
}

static bool cell_of(double x, double y, int* row, int* col) {
    if (x < 0 || y < 0) return false;
    int c = (int)(x / CELL_W), r = (int)(y / CELL_H);
    if (r < 0 || r >= AnsiTerminal::ROWS || c < 0 || c >= AnsiTerminal::COLS) return false;
    *row = r + 1; *col = c + 1;   // 1-based, matches ANSI cursor addressing
    return true;
}

static gboolean on_canvas_button(GtkWidget*, GdkEventButton* ev, gpointer) {
    if (!app.ansi_mode) return FALSE;
    int row, col;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 1 && cell_of(ev->x, ev->y, &row, &col))
        desktop_send_mouse(1, row, col);
    return TRUE;
}

// Hover is throttled: pointer-motion updates a pending cell and a ~30ms timer
// flushes at most one hover event per cell change, bounding serial traffic.
static gboolean flush_hover(gpointer) {
    app.hover_scheduled = false;
    if (app.ansi_mode && app.pend_r > 0 &&
        (app.pend_r != app.hover_r || app.pend_c != app.hover_c)) {
        app.hover_r = app.pend_r; app.hover_c = app.pend_c;
        desktop_send_mouse(0, app.hover_r, app.hover_c);
    }
    return FALSE;
}

static gboolean on_canvas_motion(GtkWidget*, GdkEventMotion* ev, gpointer) {
    if (!app.ansi_mode) return FALSE;
    int row, col;
    if (!cell_of(ev->x, ev->y, &row, &col)) return FALSE;
    app.pend_r = row; app.pend_c = col;
    if (!app.hover_scheduled) { app.hover_scheduled = true; g_timeout_add(30, flush_hover, nullptr); }
    return TRUE;
}

// --- Tag management
// ===================================================================

static GtkTextTag* make_tag(const std::string& name, const std::string& fg,
                            const std::string& bg = "", bool bold = false,
                            double scale = 1.0) {
    GtkTextTag* tag = gtk_text_buffer_create_tag(app.buf, name.c_str(), nullptr);
    if (!fg.empty()) g_object_set(tag, "foreground", fg.c_str(), nullptr);
    if (!bg.empty()) g_object_set(tag, "background", bg.c_str(), nullptr);
    if (bold) g_object_set(tag, "weight", PANGO_WEIGHT_BOLD, nullptr);
    if (scale != 1.0) g_object_set(tag, "scale", scale, nullptr);
    return tag;
}

static void setup_tags() {
    app.tags[app.tag_count++] = make_tag("boot",          "#00FF88");
    app.tags[app.tag_count++] = make_tag("kern",          "#66B2FF");
    app.tags[app.tag_count++] = make_tag("error",         "#FF4444", "", true);
    app.tags[app.tag_count++] = make_tag("info",          "#FFD700");
    app.tags[app.tag_count++] = make_tag("normal",        "#E0E0E0");
    app.tags[app.tag_count++] = make_tag("banner",        "#FF88FF", "", true);
    app.tags[app.tag_count++] = make_tag("cmd",           "#6A9955");
    app.tags[app.tag_count++] = make_tag("ts",            "#666666", "", false, 0.8);
    app.tags[app.tag_count++] = make_tag("dbg",           "#888888", "", false, 0.85);
    app.tags[app.tag_count++] = make_tag("desktop_bg",    "#FFFFFF", "#0000AA");
    app.tags[app.tag_count++] = make_tag("desktop_window", "#000000", "#FFFFFF");
    app.tags[app.tag_count++] = make_tag("desktop_title", "#FFFFFF", "#0000AA", true);
    app.tags[app.tag_count++] = make_tag("desktop_taskbar","#FFFF00", "#0000AA", true);
    app.tags[app.tag_count++] = make_tag("desktop_dark",  "#000000", "#0000AA", true);
    app.tags[app.tag_count++] = make_tag("desktop_gray",  "#FFFFFF", "#808080");
}

// ===================================================================
// CSS
// ===================================================================

static void apply_css() {
    char css[2048];
    snprintf(css, sizeof(css),
        "window { background-color: #1E1E1E; }\n"
        "#term, #term text { background-color: #0D0D0D; color: #E0E0E0; }\n"
        "GtkScrolledWindow { border: 1px solid #3E3E3E; }\n"
        "#term { font-family: \"Noto Sans Mono CJK SC\", \"Noto Sans Mono\", "
        "\"DejaVu Sans Mono\", monospace; font-size: %dpx; padding: 6px; }\n"
        "#prompt { font-family: monospace; font-weight: bold; font-size: 14px; color: #66B2FF; }\n"
        "#input-bar { background: #252526; border-top: 1px solid #3E3E3E; padding: 4px; }\n"
        "#info-frame, #info-frame > *, #info-frame GtkLabel { background: #1E1E1E; color: #D4D4D4; }\n",
        app.font_pt);

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// ===================================================================
// Permission fix via pkexec (native PolKit password dialog)
// ===================================================================

static void fix_permissions(const std::string& port) {
    append_line("[MONITOR] Requesting admin permission via pkexec...\n", "info");
    std::string cmd = "pkexec chmod 666 " + port;
    int ret = system(cmd.c_str());
    if (ret == 0) {
        append_line("[MONITOR] Permission granted, reconnecting...\n", "info");
        g_timeout_add(500, [](gpointer) -> gboolean {
            do_connect(app.current_port, app.current_baud);
            return FALSE;
        }, nullptr);
    } else {
        append_line("[MONITOR] Permission denied. Run manually:\n"
                    "  sudo usermod -a -G dialout $USER  (then re-login)\n"
                    "  sudo chmod 666 " + port + "  (temporary)\n", "error");
    }
}

// ===================================================================
// Serial reader thread
// ===================================================================

static void reader_loop() {
    std::string buf;
    char raw[256];
    while (app.running) {
        int fd = app.serial_fd;
        if (fd < 0) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {0, 100000};  // 100ms timeout
        int ret = select(fd + 1, &rfds, nullptr, nullptr, &tv);

        if (ret <= 0) {
            // Timeout — flush buffer for ANSI desktop (no newlines)
            if (!buf.empty()) {
                std::string text = buf;
                buf.clear();
                g_idle_add([](gpointer d) -> gboolean {
                    process_line(*(std::string*)d);
                    delete (std::string*)d;
                    return FALSE;
                }, new std::string(text));
            }
            continue;
        }

        int n = read(fd, raw, sizeof(raw));
        if (n <= 0) {
            g_idle_add([](gpointer) -> gboolean {
                debug_msg("Serial connection lost");
                do_disconnect();
                append_line("[MONITOR] Connection lost\n", "error");
                if (app.auto_reconnect) {
                    debug_msg("Auto-reconnect enabled, restarting");
                    start_auto_connect();
                }
                return FALSE;
            }, nullptr);
            return;
        }

        app.bytes_rx += n;
        g_idle_add([](gpointer) -> gboolean { update_rxtx(); return FALSE; }, nullptr);
        g_idle_add([](gpointer) -> gboolean {
            if (app.debug_mode) {
                char msg[64]; snprintf(msg, sizeof(msg), "RX %ld bytes", (long)app.bytes_rx);
                debug_msg(msg);
            }
            return FALSE;
        }, nullptr);

        if (app.hex_mode) {
            std::string hex;
            char tmp[8];
            for (int i = 0; i < n; i++) {
                snprintf(tmp, sizeof(tmp), "%02X ", (unsigned char)raw[i]);
                hex += tmp;
            }
            hex += " |";
            for (int i = 0; i < n; i++)
                hex += (raw[i] >= 32 && raw[i] < 127) ? (char)raw[i] : '.';
            hex += "|";
            g_idle_add([](gpointer d) -> gboolean {
                process_line(*(std::string*)d);
                delete (std::string*)d;
                return FALSE;
            }, new std::string(hex));
            continue;
        }

        buf.append(raw, n);
        // Split on newlines
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            g_idle_add([](gpointer d) -> gboolean {
                process_line(*(std::string*)d);
                delete (std::string*)d;
                return FALSE;
            }, new std::string(line));
        }
        // Flush oversized buffer (continuous data without newlines)
        if (buf.size() > 4096) {
            std::string text = buf;
            buf.clear();
            g_idle_add([](gpointer d) -> gboolean {
                process_line(*(std::string*)d);
                delete (std::string*)d;
                return FALSE;
            }, new std::string(text));
        }
    }
}

// ===================================================================
// Process line
// ===================================================================

static void process_line(const std::string& text) {
    if (app.paused) return;

    // Parse info
    std::regex boot_mode_r(R"([Bb]oot mode:\s*(\w+))");
    std::regex source_r(R"(source:\s*(\w+))");
    std::regex crystal_r(R"([Cc]rystal:\s*(\d+)\s*MHz)");
    std::regex sram_r(R"(SRAM:\s*(\d+)\s*KB)");
    std::regex heap_r(R"([Hh]eap:\s*(\d+)\s*/\s*(\d+)\s*bytes)");
    std::regex drivers_r(R"(Drivers:\s*(.+))");
    std::regex entry_r(R"(entry:\s*0x([0-9A-Fa-f]+))");
    std::regex uptime_r(R"([Uu]ptime:\s*([\d.]+[ums]?))");

    std::smatch m;
    if (std::regex_search(text, m, boot_mode_r)) update_info(1, m[1].str());
    if (std::regex_search(text, m, source_r))    update_info(2, m[1].str());
    if (std::regex_search(text, m, crystal_r))   update_info(3, m[1].str() + "MHz");
    if (std::regex_search(text, m, sram_r))       update_info(4, m[1].str() + "KB");
    if (std::regex_search(text, m, heap_r)) {
        char h[32]; snprintf(h, sizeof(h), "%sKB / %sKB",
            m[1].str().c_str(), m[2].str().c_str());
        update_info(5, h);
    }
    if (std::regex_search(text, m, drivers_r))   update_info(7, m[1].str());
    if (std::regex_search(text, m, entry_r))     update_info(8, "0x" + m[1].str());
    if (std::regex_search(text, m, uptime_r))    update_info(9, m[1].str());

    if (text.find("ESPOS Kernel v") != std::string::npos) {
        std::regex v_r(R"(v[\d.]+)");
        if (std::regex_search(text, m, v_r)) update_info(6, m[0].str());
        update_info(0, _("运行中","Running"));
    }
    if (text.find("=== ESP32-C6 Bootloader") != std::string::npos) update_info(0, _("引导中","Booting"));
    if (text.find("[BOOT][KERN] Jumping") != std::string::npos)    update_info(0, _("加载内核","Loading kernel"));
    if (text.find("System ready") != std::string::npos)            update_info(0, _("运行中","Running"));
    if (text.find("KERNEL PANIC") != std::string::npos)           update_info(0, _("内核恐慌","KERNEL PANIC"));
    if (text.find("[BOOT][EMRG]") != std::string::npos || text.find("EMERGENCY") != std::string::npos)
        update_info(0, _("紧急模式","Emergency"));

    // Desktop mode: ANSI cursor/color escapes → render on the graphical canvas.
    bool has_esc = text.find("\x1b[") != std::string::npos;
    if (has_esc || app.ansi_mode) {
        if (!app.ansi_mode) {
            app.ansi_mode = true;
            app.ansi.clear();
            gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "desk");
        }
        app.ansi.feed(text);
        if (text.find("Exited desktop") != std::string::npos ||
            text.find("Terminal launched") != std::string::npos) {
            app.ansi_mode = false;
            app.hover_r = app.hover_c = app.pend_r = app.pend_c = -1;
            gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "term");
            // Echo the exit message into the text terminal (ANSI stripped).
            std::string plain = std::regex_replace(text, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), "");
            plain.erase(0, plain.find_first_not_of(" \t\r\n"));
            size_t e = plain.find_last_not_of(" \t\r\n");
            if (e != std::string::npos) plain.erase(e + 1);
            if (!plain.empty()) append_line(plain + "\n", "info");
            return;
        }
        gtk_widget_queue_draw(app.canvas);
        return;
    }

    // Normal scrolling mode
    std::string tag = "normal";
    if (text.find("❯ ") == 0) tag = "cmd";
    else if (text.find("[BOOT]") != std::string::npos) tag = "boot";
    else if (text.find("[KERN]") != std::string::npos) tag = "kern";
    else if (text.find("[ERR]") != std::string::npos || text.find("FATAL") != std::string::npos ||
             text.find("PANIC") != std::string::npos) tag = "error";
    else if (text.find("=====") != std::string::npos) tag = "banner";

    if (app.show_timestamps) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        struct tm* lt = localtime(&t);
        char ts[32];
        snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03ld] ",
                 lt->tm_hour, lt->tm_min, lt->tm_sec, ms.count());

        GtkTextIter end;
        gtk_text_buffer_get_end_iter(app.buf, &end);
        gtk_text_buffer_insert_with_tags_by_name(app.buf, &end, ts, -1, "ts", nullptr);
        gtk_text_buffer_get_end_iter(app.buf, &end);
        gtk_text_buffer_insert_with_tags_by_name(app.buf, &end, (text + "\n").c_str(), -1, tag.c_str(), nullptr);
    } else {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(app.buf, &end);
        gtk_text_buffer_insert_with_tags_by_name(app.buf, &end, (text + "\n").c_str(), -1, tag.c_str(), nullptr);
    }

    // Trim to 5000 lines
    int lc = gtk_text_buffer_get_line_count(app.buf);
    if (lc > 5000) {
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(app.buf, &s, lc - 4000);
        gtk_text_buffer_get_iter_at_line(app.buf, &e, 0);
        gtk_text_buffer_delete(app.buf, &s, &e);
    }

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app.buf, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app.tv), &end, 0.0, FALSE, 0, 0);
}

// ===================================================================
// UI helpers
// ===================================================================

static void append_line(const std::string& text, const std::string& tag_name) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app.buf, &end);
    gtk_text_buffer_insert_with_tags_by_name(app.buf, &end, text.c_str(), -1, tag_name.c_str(), nullptr);
    gtk_text_buffer_get_end_iter(app.buf, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app.tv), &end, 0.0, FALSE, 0, 0);
}

static void debug_msg(const std::string& msg) {
    if (app.debug_mode)
        append_line("[MONITOR] " + msg + "\n", "dbg");
}

static void update_rxtx() {
    char lbl[64];
    snprintf(lbl, sizeof(lbl), "<span color=\"#888\">RX:%ld TX:%ld</span>",
             app.bytes_rx.load(), app.bytes_tx.load());
    gtk_label_set_markup(GTK_LABEL(app.rxtx_label), lbl);
}

static void set_status(const std::string& text, const std::string& color) {
    char lbl[256];
    snprintf(lbl, sizeof(lbl), "<span color=\"%s\">%s</span>", color.c_str(), text.c_str());
    gtk_label_set_markup(GTK_LABEL(app.status_label), lbl);
}

static void update_info(int idx, const std::string& value) {
    if (idx < 0 || idx >= 10) return;
    app.info_vals[idx] = value;
    if (idx == 0) {
        // Status with color
        const char* c = "#D4D4D4";
        std::string v = value;
        if (v == _("已连接","Connected") || v == _("运行中","Running")) c = "#4EC9B0";
        else if (v == _("引导中","Booting") || v == _("加载内核","Loading kernel")) c = "#DCDCAA";
        else if (v == _("未连接","Disconnected")) c = "#888888";
        else if (v == _("内核恐慌","KERNEL PANIC") || v == _("紧急模式","Emergency")) c = "#F44747";
        char lbl[256];
        snprintf(lbl, sizeof(lbl), "<span color=\"%s\"><b>%s</b></span>", c, value.c_str());
        gtk_label_set_markup(GTK_LABEL(app.info_labels[idx]), lbl);
    } else {
        gtk_label_set_text(GTK_LABEL(app.info_labels[idx]), value.c_str());
    }
}

// ===================================================================
// Reset the ESP32-C6
// ===================================================================
// On this board (CH343) RTS drives EN (reset) and DTR drives GPIO9 (boot
// select). DTR MUST stay deasserted, or GPIO9 goes low and the chip enters
// ROM download mode instead of running the bootloader (see how_to_read.MD
// pitfall #14). So: keep DTR cleared, pulse RTS only.
static void reset_chip() {
    if (app.serial_fd < 0) return;
    int dtr = TIOCM_DTR, rts = TIOCM_RTS;
    ioctl(app.serial_fd, TIOCMBIC, &dtr);   // DTR deasserted -> GPIO9 high = normal boot
    ioctl(app.serial_fd, TIOCMBIS, &rts);   // RTS asserted   -> EN low = hold in reset
    usleep(120000);
    ioctl(app.serial_fd, TIOCMBIC, &rts);   // RTS deasserted -> EN high = run
    app.bytes_rx = 0;
}

// ===================================================================
// Connect / Disconnect
// ===================================================================

static void do_connect(const std::string& port, int baud) {
    stop_auto_connect();
    app.ansi_mode = false;
    app.ansi.clear();

    app.serial_fd = serial_open(port, baud);
    if (app.serial_fd < 0) {
        std::string err = strerror(errno);
        append_line("[MONITOR] " + err + "\n", "error");
        if (errno == EACCES) {
            // Permission denied — try pkexec
            fix_permissions(port);
        }
        start_auto_connect();
        return;
    }

    app.running = true;
    app.bytes_rx = 0;
    app.bytes_tx = 0;
    app.current_port = port;
    app.current_baud = baud;
    gtk_button_set_label(GTK_BUTTON(app.connect_btn), _("断开","Disconnect"));
    char st[64]; snprintf(st, sizeof(st), _("已连接 @ %d","Connected @ %d"), baud);
    set_status(st, "#4EC9B0");
    update_rxtx();

    app.reader_thread = new std::thread(reader_loop);
    debug_msg("Connected → " + port + " @ " + std::to_string(baud));

    // Reset the board so we capture a clean boot (RTS pulse, DTR held low).
    reset_chip();

    app.query_timer = g_timeout_add(3000, [](gpointer) -> gboolean {
        // Send query commands
        if (app.serial_fd >= 0) {
            std::string cmds = "info\r\ndevice\r\nversion\r\nuptime\r\n";
            write(app.serial_fd, cmds.c_str(), cmds.size());
        }
        return FALSE;
    }, nullptr);
    app.no_data_timer = g_timeout_add(5000, [](gpointer) -> gboolean {
        if (app.bytes_rx == 0 && app.serial_fd >= 0)
            append_line("[MONITOR] Connected but no data received — check ESP power/baud rate\n", "error");
        return FALSE;
    }, nullptr);
}

static void do_disconnect() {
    app.running = false;
    app.ansi_mode = false;
    app.ansi.clear();
    if (app.no_data_timer) { g_source_remove(app.no_data_timer); app.no_data_timer = 0; }
    if (app.query_timer) { g_source_remove(app.query_timer); app.query_timer = 0; }

    if (app.reader_thread) {
        app.reader_thread->join();
        delete app.reader_thread;
        app.reader_thread = nullptr;
    }
    if (app.serial_fd >= 0) {
        close(app.serial_fd);
        app.serial_fd = -1;
    }
    gtk_button_set_label(GTK_BUTTON(app.connect_btn), _("连接","Connect"));
    set_status(_("未连接","Disconnected"), "#888");
    update_rxtx();
    debug_msg("Disconnected");
}

// ===================================================================
// Auto-connect
// ===================================================================

static void start_auto_connect() {
    if (app.auto_reconnect) {
        set_status(_("搜索设备中...","Searching..."), "#DCDCAA");
        app.reconnect_timer = g_timeout_add(1500, [](gpointer) -> gboolean {
            if (!app.auto_reconnect || app.serial_fd >= 0) return FALSE;
            auto ports = list_ports();
            if (!ports.empty()) {
                do_connect(ports[0], app.current_baud);
                return FALSE;
            }
            return TRUE;
        }, nullptr);
    }
}

static void stop_auto_connect() {
    if (app.reconnect_timer) {
        g_source_remove(app.reconnect_timer);
        app.reconnect_timer = 0;
    }
}

// ===================================================================
// Send command
// ===================================================================

static void send_cmd(const std::string& cmd) {
    append_line("❯ " + cmd + "\n", "cmd");
    if (app.serial_fd >= 0) {
        std::string data = cmd + "\r\n";
        write(app.serial_fd, data.c_str(), data.size());
        app.bytes_tx += data.size();
        update_rxtx();
        debug_msg("TX " + std::to_string(data.size()) + " bytes: " + cmd);
    }
}

// ===================================================================
// Command completion + input history
// ===================================================================

// Known shell commands (from kernel console dispatch) plus a few common
// sub-command forms, used for Tab completion and the dropdown.
static const char* CMDS[] = {
    "help", "cmd", "gui", "ls", "mkdir", "newfile", "cd", "pwd", "rename",
    "com", "net", "part", "disk", "device", "chdiv", "wifi", "reboot", "info",
    "uptime", "mode", "drivers", "log", "version", "user", "su", "export",
    "env", "echo", "ps", "pki", "wifisearch", "wifiinfo", "btscan", "uartstat",
    "ping", "dhcp", "track", "curl", "desktop", "edit",
    // common sub-command forms
    "wifi -scan", "wifi -nf", "wifi -on", "wifi -off", "wifi -status", "wifi -cal",
    "cmd -list", "part -list", "ps -l", "disk -free", "mode network", "mode serial",
    "cd /", nullptr
};

static GtkEntryCompletion* build_completion() {
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    for (int i = 0; CMDS[i]; i++) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, CMDS[i], -1);
    }
    GtkEntryCompletion* comp = gtk_entry_completion_new();
    gtk_entry_completion_set_model(comp, GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_entry_completion_set_text_column(comp, 0);
    gtk_entry_completion_set_inline_completion(comp, TRUE);
    gtk_entry_completion_set_popup_completion(comp, TRUE);
    gtk_entry_completion_set_popup_single_match(comp, FALSE);
    gtk_entry_completion_set_minimum_key_length(comp, 1);
    return comp;
}

static void entry_send() {
    const char* t = gtk_entry_get_text(GTK_ENTRY(app.entry));
    if (!t || !*t) return;
    std::string s = t;
    send_cmd(s);
    if (app.history.empty() || app.history.back() != s)
        app.history.push_back(s);
    app.hist_idx = (int)app.history.size();
    gtk_entry_set_text(GTK_ENTRY(app.entry), "");
}

// Tab -> complete to the longest common prefix; Up/Down -> command history.
static gboolean on_entry_key(GtkWidget*, GdkEventKey* ev, gpointer comp) {
    if (ev->keyval == GDK_KEY_Tab || ev->keyval == GDK_KEY_ISO_Left_Tab) {
        gtk_entry_completion_insert_prefix(GTK_ENTRY_COMPLETION(comp));
        return TRUE;  // keep focus in the entry
    }
    if (ev->keyval == GDK_KEY_Up) {
        if (!app.history.empty() && app.hist_idx > 0) {
            app.hist_idx--;
            gtk_entry_set_text(GTK_ENTRY(app.entry), app.history[app.hist_idx].c_str());
            gtk_editable_set_position(GTK_EDITABLE(app.entry), -1);
        }
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Down) {
        if (app.hist_idx < (int)app.history.size() - 1) {
            app.hist_idx++;
            gtk_entry_set_text(GTK_ENTRY(app.entry), app.history[app.hist_idx].c_str());
        } else {
            app.hist_idx = (int)app.history.size();
            gtk_entry_set_text(GTK_ENTRY(app.entry), "");
        }
        gtk_editable_set_position(GTK_EDITABLE(app.entry), -1);
        return TRUE;
    }
    return FALSE;
}

// While the graphical desktop is active, forward key presses straight to the
// ESP as raw bytes (arrows, Enter, Esc, letters) so it is interactive without
// pressing Enter. Runs at the toplevel before the input entry sees the key.
static gboolean on_window_key(GtkWidget*, GdkEventKey* ev, gpointer) {
    if (!app.ansi_mode || app.serial_fd < 0) return FALSE;  // normal handling
    const char* seq = nullptr;
    char one[2] = {0, 0};
    switch (ev->keyval) {
        case GDK_KEY_Up:        seq = "\x1b[A"; break;
        case GDK_KEY_Down:      seq = "\x1b[B"; break;
        case GDK_KEY_Right:     seq = "\x1b[C"; break;
        case GDK_KEY_Left:      seq = "\x1b[D"; break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:  seq = "\r";     break;
        case GDK_KEY_Escape:    seq = "\x1b";   break;
        case GDK_KEY_Tab:       seq = "\t";     break;
        case GDK_KEY_BackSpace: seq = "\x7f";   break;
        default:
            if (ev->keyval >= 0x20 && ev->keyval <= 0x7e) { one[0] = (char)ev->keyval; seq = one; }
            break;
    }
    if (!seq) return FALSE;
    write(app.serial_fd, seq, strlen(seq));
    app.bytes_tx += strlen(seq);
    update_rxtx();
    return TRUE;  // consume — don't type into the input entry
}

// ===================================================================
// Build UI
// ===================================================================

static void build_ui() {
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), _("ESPOS 串口监视器","ESPOS Serial Monitor"));
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1024, 680);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    g_signal_connect(app.window, "destroy", G_CALLBACK(+[]{
        app.running = false;
        stop_auto_connect();
        do_disconnect();
        gtk_main_quit();
    }), nullptr);
    // Forward raw keys to the ESP while the desktop canvas is active.
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_window_key), nullptr);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    // --- Toolbar ---
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 6);
    gtk_widget_set_margin_end(toolbar, 6);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 2);

    // Port
    GtkWidget* port_lbl = gtk_label_new((std::string(_("端口","Port")) + ":").c_str());
    gtk_box_pack_start(GTK_BOX(toolbar), port_lbl, FALSE, FALSE, 0);
    app.port_combo = gtk_combo_box_text_new();
    gtk_widget_set_size_request(app.port_combo, 150, -1);
    auto ports = list_ports();
    for (auto& p : ports) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.port_combo), p.c_str());
    if (ports.empty()) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.port_combo), _("未找到串口","No serial port found"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.port_combo), 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.port_combo, FALSE, FALSE, 0);

    GtkWidget* ref_btn = gtk_button_new_with_label("↻");
    gtk_widget_set_tooltip_text(ref_btn, _("刷新","Refresh"));
    g_signal_connect(ref_btn, "clicked", G_CALLBACK(+[]{
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app.port_combo));
        auto ports = list_ports();
        for (auto& p : ports) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.port_combo), p.c_str());
        if (ports.empty()) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.port_combo),
            _("未找到串口","No serial port found"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.port_combo), 0);
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), ref_btn, FALSE, FALSE, 0);

    // Baud
    GtkWidget* baud_lbl = gtk_label_new(("  " + std::string(_("波特率","Baud")) + ":").c_str());
    gtk_box_pack_start(GTK_BOX(toolbar), baud_lbl, FALSE, FALSE, 0);
    app.baud_combo = gtk_combo_box_text_new();
    for (int b : {115200, 9600, 19200, 38400, 57600, 230400, 460800, 921600})
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.baud_combo), std::to_string(b).c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.baud_combo), 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.baud_combo, FALSE, FALSE, 0);

    // Connect/Disconnect
    app.connect_btn = gtk_button_new_with_label(_("连接","Connect"));
    g_signal_connect(app.connect_btn, "clicked", G_CALLBACK(+[]{
        if (app.serial_fd >= 0) do_disconnect();
        else {
            auto active = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.port_combo));
            int baud = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.baud_combo)));
            if (active) do_connect(active, baud ? baud : 115200);
        }
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), app.connect_btn, FALSE, FALSE, 0);

    // Reset
    GtkWidget* rst_btn = gtk_button_new_with_label(("↺ " + std::string(_("复位","Reset"))).c_str());
    g_signal_connect(rst_btn, "clicked", G_CALLBACK(+[]{
        if (app.serial_fd >= 0) {
            debug_msg("Reset (RTS pulse, DTR held low)");
            reset_chip();
            append_line("[MONITOR] --- Chip reset ---\n", "info");
        }
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), rst_btn, FALSE, FALSE, 0);

    // Clear
    GtkWidget* clr_btn = gtk_button_new_with_label(("🗑 " + std::string(_("清屏","Clear"))).c_str());
    g_signal_connect(clr_btn, "clicked", G_CALLBACK(+[]{
        gtk_text_buffer_set_text(app.buf, "", -1);
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), clr_btn, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 6);

    // Toggles (kept minimal: timestamps, pause, auto-reconnect)
    auto add_toggle = [&](const char* icon, const char* label, bool active, GCallback cb) -> GtkWidget* {
        GtkWidget* btn = gtk_toggle_button_new_with_label((std::string(icon) + " " + label).c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), active);
        gtk_widget_set_tooltip_text(btn, label);
        g_signal_connect(btn, "toggled", cb, nullptr);
        gtk_box_pack_start(GTK_BOX(toolbar), btn, FALSE, FALSE, 0);
        return btn;
    };

    app.ts_btn = add_toggle("🕐", _("时间戳","Timestamps"), true, G_CALLBACK(+[](GtkWidget* w){
        app.show_timestamps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    }));
    app.pause_btn = add_toggle("⏸", _("暂停","Pause"), false, G_CALLBACK(+[](GtkWidget* w){
        app.paused = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    }));
    app.reconnect_btn = add_toggle("🔁", _("自动重连","Auto Reconnect"), true, G_CALLBACK(+[](GtkWidget* w){
        app.auto_reconnect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    }));

    // Font size
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new(("  " + std::string(_("字体","Font")) + ":").c_str()), FALSE, FALSE, 0);
    GtkWidget* fs_minus = gtk_button_new_with_label("−");
    gtk_widget_set_size_request(fs_minus, 26, -1);
    g_signal_connect(fs_minus, "clicked", G_CALLBACK(+[]{
        app.font_pt = std::max(8, app.font_pt - 1);
        gtk_label_set_text(GTK_LABEL(app.fs_label), std::to_string(app.font_pt).c_str());
        apply_css();
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), fs_minus, FALSE, FALSE, 0);
    app.fs_label = gtk_label_new("13");
    gtk_widget_set_size_request(app.fs_label, 24, -1);
    gtk_box_pack_start(GTK_BOX(toolbar), app.fs_label, FALSE, FALSE, 0);
    GtkWidget* fs_plus = gtk_button_new_with_label("+");
    gtk_widget_set_size_request(fs_plus, 26, -1);
    g_signal_connect(fs_plus, "clicked", G_CALLBACK(+[]{
        app.font_pt = std::min(32, app.font_pt + 1);
        gtk_label_set_text(GTK_LABEL(app.fs_label), std::to_string(app.font_pt).c_str());
        apply_css();
    }), nullptr);
    gtk_box_pack_start(GTK_BOX(toolbar), fs_plus, FALSE, FALSE, 0);

    // Right side: status + counters
    app.status_label = gtk_label_new(nullptr);
    set_status(_("未连接","Disconnected"), "#888");
    gtk_box_pack_end(GTK_BOX(toolbar), app.status_label, FALSE, FALSE, 8);
    app.rxtx_label = gtk_label_new(nullptr);
    update_rxtx();
    gtk_widget_set_margin_end(app.rxtx_label, 4);
    gtk_box_pack_end(GTK_BOX(toolbar), app.rxtx_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    // --- Main area: terminal | info panel ---
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 780);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    // Terminal + desktop canvas share the left pane via a stack.
    app.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app.stack), GTK_STACK_TRANSITION_TYPE_NONE);

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    app.tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app.tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app.tv), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app.tv), GTK_WRAP_CHAR);
    gtk_widget_set_name(app.tv, "term");
    app.buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.tv));
    setup_tags();
    gtk_container_add(GTK_CONTAINER(sw), app.tv);
    gtk_stack_add_named(GTK_STACK(app.stack), sw, "term");

    // Desktop canvas (Cairo). Centered in a scrolled window so small windows scroll.
    GtkWidget* csw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(csw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_name(csw, "term");   // reuse the dark #0D0D0D background
    app.canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.canvas, AnsiTerminal::COLS * CELL_W, AnsiTerminal::ROWS * CELL_H);
    gtk_widget_set_halign(app.canvas, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app.canvas, GTK_ALIGN_CENTER);
    gtk_widget_add_events(app.canvas, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(app.canvas, "draw", G_CALLBACK(on_canvas_draw), nullptr);
    g_signal_connect(app.canvas, "button-press-event", G_CALLBACK(on_canvas_button), nullptr);
    g_signal_connect(app.canvas, "motion-notify-event", G_CALLBACK(on_canvas_motion), nullptr);
    gtk_container_add(GTK_CONTAINER(csw), app.canvas);
    gtk_stack_add_named(GTK_STACK(app.stack), csw, "desk");

    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "term");
    gtk_paned_pack1(GTK_PANED(paned), app.stack, TRUE, FALSE);

    // Info panel
    GtkWidget* frame = gtk_frame_new(_("设备信息","Device Info"));
    gtk_widget_set_name(frame, "info-frame");
    gtk_widget_set_size_request(frame, 200, -1);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    GtkWidget* isw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(isw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 8);

    for (int i = 0; i < 10; i++) {
        GtkWidget* lbl = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
        gtk_label_set_markup(GTK_LABEL(lbl), ("<b>" + app.info_labels_str[i] + "</b>").c_str());
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, i, 1, 1);
        app.info_labels[i] = gtk_label_new("-");
        gtk_label_set_xalign(GTK_LABEL(app.info_labels[i]), 0.0);
        gtk_label_set_selectable(GTK_LABEL(app.info_labels[i]), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(app.info_labels[i]), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(app.info_labels[i]), 20);
        gtk_grid_attach(GTK_GRID(grid), app.info_labels[i], 1, i, 1, 1);
    }

    gtk_container_add(GTK_CONTAINER(isw), grid);
    gtk_container_add(GTK_CONTAINER(frame), isw);
    gtk_paned_pack2(GTK_PANED(paned), frame, FALSE, FALSE);

    // --- Input bar ---
    GtkWidget* ibox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(ibox, "input-bar");
    GtkWidget* prompt = gtk_label_new("❯");
    gtk_widget_set_name(prompt, "prompt");
    gtk_box_pack_start(GTK_BOX(ibox), prompt, FALSE, FALSE, 0);
    app.entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry),
        _("输入命令  (Tab 补全, ↑↓ 历史)","Type command  (Tab = complete, ↑↓ = history)"));
    // Command completion: dropdown + Tab completes the longest common prefix.
    GtkEntryCompletion* comp = build_completion();
    gtk_entry_set_completion(GTK_ENTRY(app.entry), comp);
    g_object_unref(comp);
    g_signal_connect(app.entry, "key-press-event", G_CALLBACK(on_entry_key), comp);
    g_signal_connect(app.entry, "activate", G_CALLBACK(+[]{ entry_send(); }), nullptr);
    gtk_box_pack_start(GTK_BOX(ibox), app.entry, TRUE, TRUE, 0);
    GtkWidget* snd_btn = gtk_button_new_with_label(_("发送","Send"));
    g_signal_connect(snd_btn, "clicked", G_CALLBACK(+[]{ entry_send(); }), nullptr);
    gtk_box_pack_start(GTK_BOX(ibox), snd_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), ibox, FALSE, FALSE, 0);
}

// ===================================================================
// Main
// ===================================================================

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);
    build_ui();
    apply_css();
    gtk_widget_show_all(app.window);
    start_auto_connect();
    gtk_main();
    return 0;
}