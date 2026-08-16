# 自研内核图形用户界面（GUI）子系统实现需求说明书


## 一、概述

在现有自研内核基础上，为**ESP32系列芯片**实现一套**轻量级、从底层构建**的图形用户界面（GUI）子系统。该系统**不依赖任何现成的GUI框架**（如LVGL、emWin、TouchGFX等），**也不依赖ESP-IDF框架的`esp_lcd`等高层组件**，所有代码需直接操作ESP32的硬件寄存器（SPI、I2C、GDMA、LCD_CAM等外设），以`doc.md`为核心准则进行开发。

### 1.1 硬件平台约束

- **目标芯片**：ESP32、ESP32-S3、ESP32-C3等
- **显示接口**：SPI（4线/3线）、8位/16位并行（I80/8080）、RGB接口，另外支持网页，串口输出GUI
- **典型显示控制器**：ILI9341、ST7789、ST7735、GC9A01等，注意做网页，串口实现。
- **触摸接口**：I2C（FT5x06、GT911、CST8XX等）网页串口也要做支持

### 1.2 设计原则

- **寄存器级驱动**：所有外设驱动直接操作ESP32的寄存器（SPI、I2C、GDMA、GPIO等），不经过任何HAL层，可能需要汇编
- **零拷贝优先**：尽可能利用GDMA直接内存访问，减少CPU参与数据传输
- **内存敏感**：ESP32的SRAM有限（通常320KB~520KB），需精打细算每一字节


## 二、显示驱动

### 2.1 SPI接口LCD驱动（主要方案）

ESP32平台最常见的LCD连接方式是SPI。需实现：

- **SPI外设初始化**：直接配置ESP32的SPI寄存器（SPI1/SPI2/SPI3），设置时钟频率（建议10~40MHz）、数据模式（CPOL/CPHA）、DMA使能等
- **命令/数据切换**：通过GPIO控制DC（Data/Command）引脚，发送命令时拉低，发送数据时拉高
- **片选控制**：通过GPIO控制CS引脚，或使用硬件SPI的专用CS
- **复位序列**：支持硬件RST引脚或软件复位（ILI9341的0x01命令）
- **显示初始化序列**：根据具体LCD控制器型号（ILI9341/ST7789/ST7735等）实现对应的初始化命令序列

**核心接口**：

```c
// 底层SPI收发（直接操作寄存器）
void spi_transfer(uint8_t *tx_buf, uint8_t *rx_buf, size_t len);
// 发送命令（DC=0）
void lcd_send_cmd(uint8_t cmd);
// 发送数据（DC=1）
void lcd_send_data(uint8_t *data, size_t len);
// 设置绘图窗口
void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
// 批量像素写入（支持DMA）
void lcd_write_pixels(uint16_t *pixels, size_t count);
```

### 2.2 并行接口LCD驱动（可选）

对于I80/8080并行接口的LCD，需实现：

- **GPIO并行总线配置**：将8/16个GPIO配置为输出模式
- **写时序控制**：直接操作GPIO寄存器实现WR/RS/RD等控制信号的精确时序
- **性能优化**：利用ESP32的GPIO批量写入特性（`GPIO_OUT_W1TS`/`W1TC`寄存器）

### 2.3 RGB接口LCD驱动（可选，ESP32-S3专用）

ESP32-S3支持RGB接口LCD，需实现：

- **LCD_CAM外设配置**：直接操作ESP32-S3的LCD_CAM寄存器，配置VSYNC、HSYNC、DE、PCLK等同步信号时序
- **帧缓冲管理**：在PSRAM或SRAM中分配至少一个全屏帧缓冲
- **DMA自动扫描**：配置GDMA自动从帧缓冲读取数据并输出到RGB接口

### 2.4 帧缓冲与双缓冲机制

**双缓冲是SPI LCD避免画面撕裂的底线操作**：

- **前台缓冲（Front Buffer）** ：当前LCD正在扫描显示的内存区域
- **后台缓冲（Back Buffer）** ：CPU正在绘制下一帧的内存区域
- **缓冲交换**：通过DMA传输完成中断触发交换，不能在绘制完成后立即交换
- **PSRAM利用**：对于ESP32-S3，优先将帧缓冲分配在PSRAM中（`MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`）

```c
// 帧缓冲结构
struct framebuffer {
    uint16_t *front;      // 前台缓冲地址
    uint16_t *back;       // 后台缓冲地址
    uint16_t width;       // 屏幕宽度
    uint16_t height;      // 屏幕高度
    uint8_t bpp;          // 每像素字节数（RGB565为2）
    volatile uint8_t ready; // DMA传输完成标志
};

// 缓冲交换（在DMA中断中调用）
void fb_swap(void);
// 获取当前绘制目标
uint16_t *fb_get_draw_buffer(void);
```

### 2.5 DMA加速

- **SPI DMA**：配置ESP32的GDMA通道，实现帧缓冲到SPI的数据传输
- **传输完成中断**：注册DMA完成中断，用于触发缓冲交换和下一帧绘制
- **双缓冲+DMA+PSRAM组合**：实现SPI接口的高帧率显示


## 三、触摸屏驱动

### 3.1 I2C触摸控制器驱动

ESP32平台常见触摸控制器通过I2C接口连接：

- **I2C外设初始化**：直接配置ESP32的I2C寄存器（I2C0/I2C1），设置时钟频率（通常100kHz~400kHz）
- **典型芯片支持**：FT5x06系列、GT911、CST8XX、STMPE610
- **I2C地址注意**：厂商提供的8位地址包含读写位，ESP32的I2C驱动需要7位地址
- **多点触摸支持**：根据芯片能力实现单点/多点触摸数据读取

```c
// I2C读写（直接操作寄存器）(仅供参考)
void i2c_write(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
void i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);

// 触摸点数据结构
struct touch_point {
    uint16_t x;
    uint16_t y;
    uint8_t pressure;
    uint8_t id;          // 触摸点ID（多点触摸）
};

// 触摸扫描（在定时器或轮询中调用）
int touch_scan(struct touch_point *points, int max_points);
```


## 四、核心图形引擎（Graphics Engine）

### 4.1 绘图原语（Primitives）

在帧缓冲上实现软件渲染绘图API(性能要高)：

| 原语 | 功能说明 |
|------|----------|
| `draw_pixel(x, y, color)` | 绘制单个像素点 |
| `draw_line(x1, y1, x2, y2, color)` | 绘制直线（Bresenham算法） |
| `draw_rect(x, y, w, h, color, fill)` | 绘制矩形（空心/实心） |
| `draw_circle(x, y, r, color, fill)` | 绘制圆形（空心/实心，Bresenham算法） |
| `draw_bitmap(x, y, w, h, data)` | 绘制位图 |

**注意**：所有绘图操作需先进行裁剪（Clipping），确保不超出目标窗口边界。

### 4.2 颜色格式

- **主色深**：RGB565（16位，每像素2字节），平衡显示质量与内存占用
- **可选支持**：RGB888（24位，每像素3字节），用于高质量显示
- **颜色宏**：`RGB565(r, g, b)`、`RGB888(r, g, b)`

### 4.3 字体与文本渲染

- **内置字体**：一套等宽点阵字体（如8x16），存储在只读数据区（Flash）
- **字符渲染**：`draw_char(x, y, char, color)`、`draw_text(x, y, string, color)`
- **字体存储**：利用ESP32的Flash直接读取（通过`spi_flash_read`或内存映射）

### 4.4 裁剪与区域管理

- **裁剪矩形（Clipping Region）** ：每个绘制操作前检查目标区域是否在有效范围内
- **窗口裁剪**：确保绘制不超出目标窗口边界


## 五、窗口管理子系统

### 5.1 窗口抽象

```c
struct window {
    uint16_t id;
    uint16_t x, y;          // 窗口位置
    uint16_t width, height; // 窗口大小
    uint8_t z_order;        // Z序（层级）
    uint8_t visible;        // 可见性
    uint8_t focused;        // 焦点状态
    uint16_t *buffer;       // 窗口专属缓冲（可选，用于局部刷新）
    void (*draw)(struct window *win);      // 绘制回调
    void (*on_event)(struct window *win, struct event *ev); // 事件回调
};
```

### 5.2 窗口管理操作

| 操作 | 功能描述 |
|------|----------|
| `win_create(x, y, w, h, title)` | 创建一个新窗口 |
| `win_destroy(id)` | 销毁指定窗口，释放资源 |
| `win_move(id, new_x, new_y)` | 移动窗口位置 |
| `win_resize(id, new_w, new_h)` | 调整窗口大小 |
| `win_focus(id)` | 设置焦点至指定窗口 |
| `win_hide/show(id)` | 隐藏/显示窗口 |
| `win_bring_to_top(id)` | 将窗口置顶 |

### 5.3 事件分发机制

- **事件队列**：环形缓冲区存储待处理事件
- **事件类型**：`TOUCH_DOWN`、`TOUCH_UP`、`TOUCH_MOVE`、`KEY_DOWN`、`KEY_UP`、`WINDOW_CLOSE`
- **事件分发**：根据触摸坐标或焦点状态将事件投递至目标窗口


## 六、控件库（Widget Library，可选）

### 6.1 基础控件

根据系统资源裁剪，可选实现：

| 控件 | 说明 |
|------|------|
| **按钮（Button）** | 支持按下、释放状态，关联点击回调 |
| **标签（Label）** | 静态文本显示 |
| **文本框（Textbox）** | 支持光标移动、字符输入、删除 |
| **滚动条（Scrollbar）** | 用于长内容滚动 |
| **进度条（Progress Bar）** | 显示进度 |

### 6.2 控件接口

每个控件实现标准接口：`draw()`、`on_event(event)`。


## 七、GUI与内核命令系统集成

### 7.1 `gui`命令族

| 命令 | 功能描述 |
|------|----------|
| `gui -start` | 启动GUI服务（初始化SPI/I2C、LCD、触摸、窗口管理器） |
| `gui -stop` | 停止GUI服务，释放资源，切回控制台模式 |
| `gui -exec <app_path>` | 在GUI环境中执行图形应用程序 |
| `gui -list` | 列出当前所有存活窗口 |
| `gui -kill <win_id>` | 强制关闭指定窗口 |
| `gui -bench` | 运行显示性能测试（帧率、刷新延迟等） |
| `gui -help` | 显示帮助信息 |

### 7.2 与文件系统集成

- 图形应用存放于`/apps/`目录
- 字体资源存放于`/fonts/`目录
- 位图资源存放于`/res/`目录


## 八、性能与内存约束

### 8.1 内存占用估算（以320×240 RGB565为例）

| 项目 | 大小 | 说明 |
|------|------|------|
| 单帧缓冲 | 150KB | 320×240×2字节 |
| 双缓冲 | 300KB | 前台+后台 |
| GUI核心 | <32KB | 窗口管理、事件循环、绘图引擎 |
| 字体数据 | <8KB | 基本ASCII字符集 |

**注意**：ESP32-S3可通过PSRAM扩展帧缓冲，ESP32需精打细算SRAM分配。

### 8.2 性能目标

- **SPI 40MHz**：理论帧率约33fps（完整一帧约30ms传输时间）
- **双缓冲+DMA**：实际可达25~30fps流畅显示
- **窗口响应延迟**：<50ms

### 8.3 资源回收

- 窗口销毁时必须回收所有显存和堆内存
- DMA通道使用完毕后需正确释放


## 九、可扩展性与裁减性

- **模块化编译**：GUI子系统支持通过内核配置宏（如`CONFIG_GUI_ENABLE`）整体启用/禁用
- **功能分级**：
  - **最小模式**：仅帧缓冲绘图（无窗口管理）
  - **标准模式**：完整窗口管理 + 基础控件
  - **增强模式**：完整窗口管理 + 全部控件 + 动画支持
- **显示接口适配**：通过配置宏选择SPI/并行/RGB接口


## 十、ESP32硬件寄存器参考

实现时需直接操作的ESP32关键寄存器组：

| 外设 | 关键寄存器 | 说明 |
|------|-----------|------|
| SPI | `SPI_CMD`, `SPI_ADDR`, `SPI_DATA_BUF*` | SPI数据传输控制 |
| SPI | `SPI_PIN`, `SPI_USER` | SPI引脚配置与用户控制 |
| I2C | `I2C_COMD`, `I2C_DATA` | I2C命令与数据寄存器 |
| GDMA | `DMA_IN_LINK`, `DMA_OUT_LINK` | DMA描述符链表控制 |
| GPIO | `GPIO_OUT`, `GPIO_OUT_W1TS/W1TC` | GPIO输出与控制 |
| LCD_CAM (S3) | `LCD_CAM_LCD_CTRL`, `LCD_CAM_LCD_*` | RGB接口时序控制 |


## 十一、优化原则总结

1. **寄存器级驱动**：所有外设操作直接读写ESP32寄存器，不经过任何HAL或框架层
2. **不依赖ESP-IDF**：不使用`esp_lcd`、`esp_lcd_panel`等任何ESP-IDF组件
3. **双缓冲为底线**：SPI LCD必须实现双缓冲+DMA机制避免画面撕裂
4. **内存精打细算**：ESP32 SRAM有限，优先使用PSRAM（ESP32-S3）存放帧缓冲
5. **可参考但不复制**：可参考WROVER KIT LCD库等开源实现的思路，但实现代码必须完全自主，适配自研内核架构
6. **严格测试**：需提供基础的GUI自动化测试（模拟点击、窗口操作），验证内存安全与稳定性
7.**优先级原则**:仍需以doc.MD的基础规则为准则进行