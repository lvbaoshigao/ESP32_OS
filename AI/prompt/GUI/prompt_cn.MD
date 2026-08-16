# ESP32-C6 自研内核 GUI 子系统开发任务书

## 一、项目背景

在现有自研内核（ESPOS Kernel v0.1.0）基础上，为 ESP32-C6 实现一套**轻量级、从底层构建**的图形用户界面子系统。**不依赖任何现成 GUI 框架**（LVGL、emWin、TouchGFX 等），所有代码直接操作硬件寄存器，以 `doc.MD` 为核心准则。

### 硬件平台

| 参数 | 值 |
|------|------|
| MCU | ESP32-C6 (RISC-V RV32IMAC) |
| SRAM | 512KB HP SRAM + 16KB LP SRAM |
| 显示接口 | SPI（首选）、串口输出、Web 页面 |
| 触摸接口 | I2C（FT5x06/GT911/CST8XX）、串口、Web |
| 存储 | 8MB 内嵌 Flash |

### 设计原则

- **寄存器级驱动**：所有外设操作直接读写 ESP32-C6 寄存器，不经过任何 HAL 或框架层
- **零拷贝优先**：利用 GDMA 直接内存访问，减少 CPU 参与数据传输
- **内存敏感**：512KB SRAM 需精打细算，帧缓冲优先使用外部 PSRAM（若可用）
- **模块化编译**：GUI 子系统通过内核配置宏整体启用/禁用

---

## 二、代码结构

```
kernel/
├── include/
│   └── gui.h                  # GUI 子系统对外接口
├── src/
│   ├── gui/
│   │   ├── gui_core.cpp       # 初始化、事件循环、主控
│   │   ├── gui_draw.cpp       # 绘图原语（像素、线、矩形、圆、位图）
│   │   ├── gui_font.cpp       # 点阵字体渲染
│   │   ├── gui_window.cpp     # 窗口管理子系统
│   │   ├── gui_fb.cpp         # 帧缓冲管理（双缓冲 + DMA）
│   │   ├── gui_lcd_spi.cpp    # SPI 接口 LCD 驱动
│   │   ├── gui_touch_i2c.cpp  # I2C 触摸驱动
│   │   ├── gui_widget.cpp     # 基础控件库（按钮、标签）
│   │   └── gui_cmd.cpp        # gui 命令族（外壳集成）
│   └── console.cpp            # 修改：添加 gui 命令分发
```

---

## 三、模块一：帧缓冲管理（`gui_fb.cpp`）

### 功能需求

实现双缓冲机制，避免画面撕裂。

### 接口

```c
// 帧缓冲结构
struct framebuffer {
    uint16_t *front;           // 前台缓冲地址
    uint16_t *back;            // 后台缓冲地址
    uint16_t width;            // 屏幕宽度
    uint16_t height;           // 屏幕高度
    uint8_t bpp;               // 每像素字节数（RGB565=2）
    volatile uint8_t ready;    // DMA 传输完成标志
};

// 初始化帧缓冲（分配内存）
int fb_init(uint16_t width, uint16_t height);

// 缓冲交换（在 DMA 中断中调用）
void fb_swap(void);

// 获取当前绘制目标
uint16_t* fb_get_draw_buffer(void);

// 释放帧缓冲
void fb_deinit(void);
```

### 实现要求

- 使用 `mm_alloc` 分配帧缓冲内存
- 双缓冲：前台缓冲用于 LCD 扫描，后台缓冲用于 CPU 绘制
- DMA 传输完成后通过中断触发缓冲交换
- 320×240 RGB565 双缓冲约需 300KB（150KB×2）

---

## 四、模块二：SPI LCD 驱动（`gui_lcd_spi.cpp`）

### 功能需求

通过 SPI 接口驱动 LCD 显示屏，支持常见控制器（ILI9341、ST7789、ST7735、GC9A01）。

### 接口

```c
// 初始化 LCD（GPIO/SPI/时序初始化）
int lcd_init(void);

// 发送命令（DC=0）
void lcd_send_cmd(uint8_t cmd);

// 发送数据（DC=1）
void lcd_send_data(uint8_t *data, size_t len);

// 设置绘图窗口
void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// 批量像素写入（支持 DMA）
void lcd_write_pixels(uint16_t *pixels, size_t count);
```

### 实现要求

- **GPIO 配置**：直接操作 ESP32-C6 GPIO 寄存器，配置 DC、CS、RST、SCLK、MOSI 引脚
- **SPI 配置**：直接操作 SPI 寄存器（SPI2），设置时钟频率（10~40MHz）、模式 0（CPOL=0,CPHA=0）、MSB 先行
- **硬件 SPI 收发**：通过 SPI 寄存器实现全双工收发，支持 GDMA
- **初始化序列**：根据 LCD 控制器型号发送对应的初始化命令序列
- **复位序列**：硬件 RST 引脚复位或软件复位

### ESP32-C6 SPI 寄存器参考

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| SPI_CMD | 0x00 | 命令寄存器，bit 18 = USR（用户定义传输） |
| SPI_ADDR | 0x04 | 地址寄存器 |
| SPI_USER | 0x10 | 用户控制寄存器 |
| SPI_USER1 | 0x14 | 用户控制寄存器 1 |
| SPI_USER2 | 0x18 | 用户控制寄存器 2 |
| SPI_MOSI_DLEN | 0x24 | MOSI 数据长度 |
| SPI_MISO_DLEN | 0x28 | MISO 数据长度 |
| SPI_W0 | 0x80 | 数据缓冲区字 0 |
| SPI_PIN | 0x08 | 引脚控制寄存器 |

---

## 五、模块三：触摸驱动（`gui_touch_i2c.cpp`）

### 功能需求

通过 I2C 接口驱动触摸控制器，支持 FT5x06、GT911、CST8XX 等。

### 接口

```c
// 触摸点数据结构
struct touch_point {
    uint16_t x;
    uint16_t y;
    uint8_t pressure;
    uint8_t id;              // 触摸点 ID（多点触摸）
};

// 初始化触摸控制器
int touch_init(void);

// 触摸扫描（在定时器或轮询中调用）
int touch_scan(struct touch_point *points, int max_points);
```

### 实现要求

- **I2C 配置**：直接操作 I2C 寄存器（I2C0），时钟频率 100~400kHz
- **地址注意**：厂商提供的 8 位地址包含读写位，需使用 7 位地址
- **多点触摸**：根据芯片能力实现单点/多点触摸数据读取
- **串口/Web 回退**：无触摸硬件时，可通过串口或 Web 模拟触摸输入

---

## 六、模块四：绘图引擎（`gui_draw.cpp`）

### 功能需求

在帧缓冲上实现软件渲染绘图 API，性能优先。

### 接口

```c
// 绘制单个像素点
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);

// 绘制直线（Bresenham 算法）
void draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

// 绘制矩形
void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t fill);

// 绘制圆形（Bresenham 算法）
void draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color, uint8_t fill);

// 绘制位图
void draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
```

### 颜色格式

```c
#define RGB565(r, g, b)  ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
```

### 实现要求

- 所有绘图操作前先进行裁剪（Clipping），确保不超出帧缓冲边界
- 实心填充优先使用内存填充（`k_memset`）而非逐点绘制
- 直线和圆形使用 Bresenham 算法，避免浮点运算

---

## 七、模块五：字体渲染（`gui_font.cpp`）

### 功能需求

提供等宽点阵字体渲染能力。

### 接口

```c
// 绘制单个字符
void draw_char(uint16_t x, uint16_t y, char c, uint16_t color);

// 绘制字符串
void draw_text(uint16_t x, uint16_t y, const char *str, uint16_t color);
```

### 实现要求

- 内置 8×16 等宽点阵字体（ASCII 可打印字符 0x20~0x7E）
- 字体数据以位图数组形式存储在只读数据区（Flash）
- 每个字符 16 字节（8×16 像素，1bit 每像素）
- 支持前景色设置

---

## 八、模块六：窗口管理（`gui_window.cpp`）

### 功能需求

提供基本的窗口管理能力。

### 接口

```c
struct window {
    uint16_t id;
    uint16_t x, y;           // 窗口位置
    uint16_t width, height;  // 窗口大小
    uint8_t z_order;         // Z 序（层级）
    uint8_t visible;         // 可见性
    uint8_t focused;         // 焦点状态
    uint16_t *buffer;        // 窗口专属缓冲（可选，用于局部刷新）
    void (*draw)(struct window *win);
    void (*on_event)(struct window *win, struct event *ev);
};

// 窗口管理操作
int  win_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *title);
int  win_destroy(uint16_t id);
int  win_move(uint16_t id, uint16_t new_x, uint16_t new_y);
int  win_resize(uint16_t id, uint16_t new_w, uint16_t new_h);
int  win_focus(uint16_t id);
int  win_hide(uint16_t id);
int  win_show(uint16_t id);
int  win_bring_to_top(uint16_t id);
```

### 事件分发

```c
enum event_type {
    EVENT_TOUCH_DOWN,
    EVENT_TOUCH_UP,
    EVENT_TOUCH_MOVE,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_WINDOW_CLOSE
};

struct event {
    enum event_type type;
    uint16_t x, y;           // 触摸坐标
    uint32_t key;            // 按键码
    uint32_t timestamp;
};
```

### 实现要求

- 事件队列使用环形缓冲区
- 根据触摸坐标或焦点状态将事件分发至目标窗口
- 窗口绘制时按 Z 序从低到高渲染

---

## 九、模块七：基础控件库（`gui_widget.cpp`）

### 功能需求

提供基础 GUI 控件。

### 接口

```c
// 按钮控件
struct button {
    struct window win;
    const char *text;        // 按钮文本
    uint16_t bg_color;       // 背景色
    uint16_t text_color;     // 文本色
    uint8_t pressed;         // 按下状态
    void (*on_click)(void);  // 点击回调
};

// 标签控件
struct label {
    struct window win;
    const char *text;
    uint16_t text_color;
};

// 创建按钮
int button_create(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  const char *text, void (*on_click)(void));

// 创建标签
int label_create(uint16_t x, uint16_t y, const char *text);
```

### 实现要求

- 按钮支持按下/释放状态，视觉反馈
- 标签为静态文本显示
- 控件实现标准接口：`draw()`、`on_event()`

---

## 十、模块八：GUI 命令集成（`gui_cmd.cpp`）

### 功能需求

将 GUI 子系统集成到内核命令系统。

### 命令规范

| 命令 | 功能 |
|------|------|
| `gui -start` | 启动 GUI 服务（初始化 SPI/I2C/LCD/触摸/窗口管理器） |
| `gui -stop` | 停止 GUI 服务，释放资源，切回控制台模式 |
| `gui -exec <app_path>` | 在 GUI 环境中执行图形应用程序 |
| `gui -list` | 列出当前所有存活窗口 |
| `gui -kill <win_id>` | 强制关闭指定窗口 |
| `gui -bench` | 运行显示性能测试（帧率、刷新延迟） |
| `gui -help` | 显示帮助信息 |

### 实现要求

- `gui -start` 初始化所有硬件，在帧缓冲上绘制桌面背景（纯蓝色）
- `gui -stop` 关闭所有窗口，释放帧缓冲内存，恢复控制台
- `gui -bench` 绘制渐变或运动图形，统计帧率

---

## 十一、集成与构建

### 目录结构

GUI 源代码统一放在 `kernel/src/gui/` 目录下，头文件在 `kernel/include/`。

### Makefile 修改

```makefile
# 新增 GUI 源文件
GUI_SRCS = src/gui/gui_core.cpp src/gui/gui_draw.cpp src/gui/gui_font.cpp \
           src/gui/gui_window.cpp src/gui/gui_fb.cpp src/gui/gui_lcd_spi.cpp \
           src/gui/gui_touch_i2c.cpp src/gui/gui_widget.cpp src/gui/gui_cmd.cpp

CXX_SRCS = ... $(GUI_SRCS)
```

### 配置宏

```c
// include/gui.h
#define CONFIG_GUI_ENABLE   1    // 0=禁用GUI, 1=启用
#define CONFIG_GUI_LCD_W    320  // LCD 宽度
#define CONFIG_GUI_LCD_H    240  // LCD 高度
#define CONFIG_GUI_LCD_SPI  2    // SPI 外设编号
#define CONFIG_GUI_TOUCH_I2C 0   // I2C 外设编号
```

---

## 十二、开发顺序

| 阶段 | 模块 | 依赖 | 说明 |
|------|------|------|------|
| P0 | 帧缓冲管理 | 无 | 双缓冲分配与交换 |
| P0 | SPI LCD 驱动 | 帧缓冲 | 点亮屏幕，显示纯色 |
| P0 | 绘图引擎 | 帧缓冲 | 绘制像素、线、矩形、圆 |
| P0 | 字体渲染 | 绘图引擎 | 显示文本 |
| P1 | 窗口管理 | 绘图引擎 | 窗口创建/移动/销毁 |
| P1 | GUI 命令集成 | 窗口管理 | `gui -start`/`-stop`/`-list`/`-help` |
| P2 | 触摸驱动 | 窗口管理 | 触摸输入支持 |
| P2 | 基础控件 | 窗口管理 | 按钮、标签 |

---

## 十三、开发限制

1. **不依赖 ESP-IDF**：不使用 `esp_lcd`、`esp_lcd_panel` 等任何 ESP-IDF 组件
2. **不依赖现成 GUI 框架**：不使用 LVGL、emWin、TouchGFX 等
3. **寄存器级驱动**：所有外设操作直接读写 ESP32-C6 寄存器
4. **内存精打细算**：512KB SRAM 有限，帧缓冲大小需根据实际可用内存调整
5. **可参考但不复制**：可参考开源实现思路，但代码必须完全自主编写
6. **以 doc.MD 为最高准则**：若有冲突，以 `doc.MD` 为准