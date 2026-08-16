# Update 3 — Core Utility Commands + Desktop Package Manager

> Status: **spec / implementation notes** for this change set.
> Basis: `AI/doc.MD` (highest priority — no ESP-IDF, original code, verified APIs),
> `AI/how_to_read.MD`, `AI/prompt/software/prompt_en.MD` (PKI = `.espapp` package
> manager installing into `/app/<name>/`).
> Language: English.

## 1. Motivation

Two user requests:
1. **Tidy the desktop icons** — arrange them like real desktop icons (a uniform,
   top-left-anchored grid instead of the previous hand-placed, unevenly-spaced
   tiles).
2. **A real Package Manager UI in desktop mode** — clicking the PKI icon used to
   dump `pki -help` into the shell; it should open an in-desktop window that
   lists installed packages and can remove them.
3. **More shell commands** — the shell had no `cat`/`rm`/`cp`/`clear`, etc. Add a
   set of core utilities, all built on the existing VFS / MM APIs (no new
   hardware access, no ESP-IDF).

## 2. New shell commands (12)

New file **`kernel/src/coreutils.cpp`** (keeps `console.cpp` from growing further;
registered in the dispatch table in `console.cpp` and in the kernel `Makefile`).
All reuse existing kernel APIs; none touch registers or ESP-IDF.

| Command | Behaviour | Reuses |
|---|---|---|
| `cat <file>` | Print file contents to the console | `vfs_read` |
| `rm <path>` | Delete a file or directory | `vfs_delete` |
| `cp <src> <dst>` | Copy a file | `vfs_read` + `vfs_mkfile` + `vfs_write` |
| `clear` | Clear the screen (ANSI `2J`+home) | `uart_puts` |
| `whoami` | Show current user + level (root/user) | `g_username`, `g_user_level` |
| `free` | Heap used/free/total (+ zram/swap if present) | `mm_free_bytes/total_bytes`, `mm_swap_info` |
| `hexdump <file>` | 16-byte-per-row hex + ASCII dump | `vfs_read` |
| `wc <file>` | Line / word / byte counts | `vfs_read` |
| `find <name>` | Search the whole VFS tree, print full paths of matches (substring) | new VFS iteration API |
| `calc <expr>` | Integer expression evaluator: `+ - * /`, parentheses, precedence | — (self-contained recursive-descent) |
| `sleep <ms>` | Busy-wait delay (cooperative; blocks shell task like `ping`) | `get_mcycle`, `CYCLES_PER_US` |
| `date` | Boot-relative clock `HH:MM:SS` + seconds since boot (no RTC on board) | `get_mcycle` |

### New VFS iteration API (`vfs.cpp` + `kernel.h`)
`find` needs to walk every node, but `g_nodes[]` is private and the `vfs_list`
callback carries no context pointer. Add three small public accessors:
- `int vfs_node_count();`
- `const vfs_node_t* vfs_node_get(int idx);`
- `void vfs_path_of(int idx, char* buf, int maxlen);` (public wrapper over the
  existing internal `build_pwd`).

These are generic and reusable (not `find`-specific).

## 3. Desktop changes (`kernel/src/software.cpp`)

### 3.1 Tidy icon grid
Replace the hand-placed `{row,col}` fields in `DESK_ICONS` with a **computed
grid**: constants `GRID_COLS`, `ICON_W`, `GAP_X`, `GAP_Y`, `ORIGIN_ROW`,
`ORIGIN_COL`; each icon's position derived from its index
(`col = ORIGIN_COL + (i%GRID_COLS)*(ICON_W+GAP_X)`, row similarly). Uniform
gutters, anchored top-left → looks like a real desktop. Adding/removing icons
needs no manual repositioning. `DESK_ICONS` keeps only `{glyph, label, bg,
action}`. Hit-regions are still registered from the computed rectangle.

### 3.2 Interactive Package Manager window (new modal, `modal == 5`)
Clicking the **PKI** icon (or Apps→PKI) now opens an in-desktop window instead of
shelling out:
- On open: scan `/app` via `vfs_list("/app", cb)` into file-static
  `g_pki_names[][]` / `g_pki_count` (the callback has no context, so a
  file-static accumulator is required).
- Draw: titled window "Package Manager", one selectable row per installed
  package, a footer with the count, a `[Remove]` button, and an install hint
  (`pki -i <file>.espapp` from shell). Empty state: "No packages installed."
- Interaction (mouse + keyboard, via the existing hot-region + event loop):
  - hover / ↑↓ / click a row → select it;
  - click `[Remove]` or press `r` → root-gated `vfs_delete("/app/<name>")`, then
    rescan + redraw;
  - `Esc` / click-close → back to desktop.
- Selection/list state lives beside the existing `sel/open_menu/menu_sel/modal`
  vars; a dedicated branch in the `cmd_desktop` event loop handles `modal == 5`
  (the generic "any click closes" modal branch is bypassed for PKI).

Everything drawn stays single-byte ASCII (canvas = one byte per cell; borders use
`+ - |`). No change to the monitor is required — the mouse CSI protocol from the
previous update already covers this.

## 4. Follow-up: shared window frame + interactive File Manager + [X] close buttons

In response to "优化文件管理器的操作逻辑，优化每个窗口的显示逻辑，每个窗口增加关闭按钮"
(optimise the file manager's interaction, optimise every window's display, give every
window a close button), this change set adds a **shared window chrome** and reworks
the File Manager into a **real VFS browser**:

### 4.1 Shared window frame — `desktop_window_frame(top, col, W, h, title)`
All modal windows (About/Settings/Network/PKI/File Manager) now render through one
helper that draws the ASCII border, a gray title bar (`TITLE`), and a **bright-red
`[X]` close button** (`CLOSEBTN` = `CSI "101;97;1m"`) in the top-right. The `[X]`
registers an `ACT_CLOSE` hot-region so every window closes the same way: `[X]`,
`Esc`, `q`/`x`, or click-outside. `desktop_modal()` (used by the three static
windows) was simplified to build on it; the old hand-drawn PKI window body was
removed in favor of the frame.

Window geometry convention: top border row `top`, bottom `top+h-1`, title bar
`top+1` (title left, `[X]` at cols `col+W-4..col+W-2`), body rows `top+2..top+h-2`
white.

### 4.2 Interactive File Manager (`modal == 1`)
`desktop_show_files()` (a static list) was replaced by an interactive VFS browser
driven by the real filesystem:
- File-static accumulators (the `vfs_list` callback has no context, so the same
  pattern as the PKI scan): `g_fm_path`, `g_fm_names[][VFS_MAX_NAME]`,
  `g_fm_types[]`, `g_fm_sizes[]`, `g_fm_count`.
- `desktop_fm_scan()` lists `g_fm_path`; `desktop_fm_open()` starts at `/`;
  `fm_enter(name)` appends a component; `fm_up()` drops the last component.
- Rows: an optional `[..]` up-entry, then `[DIR] name` / `[FIL] name` rows
  (capped at `FM_VIS` rows so the window fits the 24-row screen). Each row
  registers `ACT_FM_ROW0 + display index`.
- Controls: arrows/WASD select; `Enter`/`e`/click activates — descend into a
  directory or go up via `[..]`; `Backspace`/`u` goes up a level directly.
  The status line shows `DIR <name>` or `FILE <name> (<bytes>)` for the current
  selection (byte count built by hand — the kernel has no `snprintf`).

### 4.3 Close semantics
- New action `ACT_CLOSE` (enum value above `ACT_PKI_REMOVE`).
- PKI + File Manager branches in the `cmd_desktop` loop treat `[X]` specially
  (close only, don't select a row / activate); static-modals branch closes on
  `[X]` or any click; the File Manager row/close/activate logic no longer falls
  through to "any click outside closes".
- `desktop_dispatch()` gained an `int* fm_sel` parameter (it now initializes the
  File Manager on open); `desktop_redraw_full()` and the event loop thread
  `fm_sel` alongside `pki_sel`, and the change-detection includes it.

## 5. Verification

1. Build: `cd kernel && make`; flash via the HW loop (RTS reset, DTR low).
2. Commands: over serial run each new command against VFS files
   (`newfile`/`edit` to create, then `cat`/`hexdump`/`wc`/`cp`/`rm`/`find`),
   plus `free`, `whoami`, `clear`, `calc "2*(3+4)"`, `sleep 500`, `date`.
3. Desktop: open `desktop`, confirm the icon grid is uniform; open the PKI icon,
   install a package from shell first (`pki -i`), confirm it appears in the
   window, select and Remove it (as root), confirm it disappears.
4. File Manager: open it, arrow/Enter into `/user` (has real subdirs), `[..]`
   back to `/`, confirm the status line shows file sizes; click the `[X]` (and
   `Esc`) to close each window.
5. Offline PNG render of the desktop + PKI window through the monitor's emulator
   (as in the previous update) for a visual check.

## 6. Docs to update after implementation
- `AI/how_to_read.MD`: add `kernel/src/coreutils.cpp` to the file table; note the
  new commands under `software.cpp`/`console.cpp`; extend the desktop pitfall
  (#15) with the PKI modal; add the VFS iteration API.
- `MEMORY.md` index if a durable note is warranted.
