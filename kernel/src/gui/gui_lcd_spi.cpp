#include "gui.h"
#include "kernel.h"

// === SPI LCD driver — ESP32-C6 SPI2 register-level, ILI9341 ===
// References: ESP32-C6 TRM Chapters 13 (SPI), 6 (GPIO Matrix), 7 (IO MUX)

// --- ESP32-C6 peripheral base addresses [TRM 2.4] ---
#define GPIO_BASE       0x60040400u
#define IO_MUX_BASE     0x60090800u
#define SPI2_BASE       0x6009C000u

// --- GPIO registers [TRM 6] ---
#define GPIO_ENABLE_REG     (GPIO_BASE + 0x0C)
#define GPIO_OUT_REG        (GPIO_BASE + 0x04)
#define GPIO_OUT1_REG       (GPIO_BASE + 0x10)
#define GPIO_FUNC_OUT_SEL_CFG(n)  (GPIO_BASE + 0x20 + (n) * 4)  // [TRM 6.3.16]
#define GPIO_FUNC_IN_SEL_CFG(n)   (GPIO_BASE + 0x34 + (n) * 4)  // [TRM 6.3.18]

// --- IO MUX registers [TRM 7] ---
#define IO_MUX_GPIO_REG(n)  (IO_MUX_BASE + 0x20 + (n) * 4)      // [TRM 7.3.4]
#define IO_MUX_OE           (1u << 9)
#define IO_MUX_FUNC_GPIO    0
#define IO_MUX_FUNC_FSPI    2       // FSPI function on IO MUX

// --- SPI2 registers [TRM 13] ---
#define SPI_CMD             (SPI2_BASE + 0x000)  // [TRM 13.4.1]
#define SPI_CLOCK           (SPI2_BASE + 0x00C)  // [TRM 13.4.4]
#define SPI_USER            (SPI2_BASE + 0x010)  // [TRM 13.4.5]
#define SPI_USER1           (SPI2_BASE + 0x014)  // [TRM 13.4.6]
#define SPI_USER2           (SPI2_BASE + 0x018)  // [TRM 13.4.7]
#define SPI_MOSI_DLEN       (SPI2_BASE + 0x020)  // [TRM 13.4.9]
#define SPI_MISO_DLEN       (SPI2_BASE + 0x024)  // [TRM 13.4.10]
#define SPI_W0              (SPI2_BASE + 0x080)  // [TRM 13.4.30]
#define SPI_W1              (SPI2_BASE + 0x084)
#define SPI_W2              (SPI2_BASE + 0x088)
#define SPI_W3              (SPI2_BASE + 0x08C)
#define SPI_W4              (SPI2_BASE + 0x090)
#define SPI_W5              (SPI2_BASE + 0x094)
#define SPI_SLAVE           (SPI2_BASE + 0x008)  // [TRM 13.4.3]

// SPI_CMD bits
#define SPI_CMD_USR         (1u << 18)  // start user-defined transaction

// SPI_USER bits [TRM 13.4.5]
#define SPI_USR_CMD         (1u << 31)
#define SPI_USR_MOSI        (1u << 25)
#define SPI_USR_MISO        (1u << 24)
#define SPI_DOUTDIN         (1u << 15)
#define SPI_CS_HOLD         (1u << 7)
#define SPI_CS_SETUP        (1u << 6)

// SPI_USER1 bits [TRM 13.4.6]
#define SPI_USR_CMD_BITLEN_S  26
#define SPI_USR_MOSI_BITLEN_S 13

// SPI_USER2 bits [TRM 13.4.7]
#define SPI_USR_COMMAND_BITLEN_S  28
#define SPI_USR_COMMAND_VALUE_S   0

// SPI_CLOCK bits [TRM 13.4.4]
#define SPI_CLK_EQU_SYSCLK  (1u << 31)
#define SPI_CLKCNT_N_S      18
#define SPI_CLKCNT_H_S      8
#define SPI_CLKCNT_L_S      0

// --- Pin assignments (ESP32-C6) ---
#define PIN_LCD_MOSI    11
#define PIN_LCD_MISO    12
#define PIN_LCD_SCLK    13
#define PIN_LCD_CS      10
#define PIN_LCD_DC      9
#define PIN_LCD_RST     8

// ILI9341 commands
#define ILI_CMD_SWRESET     0x01
#define ILI_CMD_SLPOUT      0x11
#define ILI_CMD_DISPON      0x29
#define ILI_CMD_MADCTL      0x36
#define ILI_CMD_PIXFMT      0x3A
#define ILI_CMD_RAMWR       0x2C

// Forward declarations
static void lcd_dc_high(void);
static void lcd_dc_low(void);
static void lcd_cs_high(void);
static void lcd_cs_low(void);
static void spi_transfer(const uint8_t *tx, uint8_t *rx, int len);
static void spi_transfer_word(uint32_t data, int bits);

static void lcd_dc_high(void) {
    REG32(GPIO_OUT_REG) |= (1u << PIN_LCD_DC);
}

static void lcd_dc_low(void) {
    REG32(GPIO_OUT_REG) &= ~(1u << PIN_LCD_DC);
}

static void lcd_cs_high(void) {
    REG32(GPIO_OUT_REG) |= (1u << PIN_LCD_CS);
}

static void lcd_cs_low(void) {
    REG32(GPIO_OUT_REG) &= ~(1u << PIN_LCD_CS);
}

static void delay_ms(uint32_t ms) {
    // 40 MHz = 40 cycles/us, 1ms = 40000 cycles
    uint32_t target = uptime_us() + ms * 1000;
    while (uptime_us() < target) asm volatile("nop");
}

// --- SPI2 transfer: send/receive up to 64 bytes via FIFO ---
static void spi_transfer(const uint8_t *tx, uint8_t *rx, int len) {
    if (len <= 0) return;

    volatile uint32_t *wbuf = (volatile uint32_t *)SPI_W0;
    int words = (len + 3) / 4;

    // Load TX data into FIFO
    for (int i = 0; i < words; i++) {
        uint32_t w = 0;
        for (int b = 0; b < 4 && i * 4 + b < len; b++) {
            w |= (tx ? tx[i * 4 + b] : 0) << (b * 8);
        }
        wbuf[i] = w;
    }

    // Configure transfer
    int bitlen = len * 8;
    REG32(SPI_USER) = SPI_USR_MOSI | SPI_USR_MISO;
    REG32(SPI_MOSI_DLEN) = bitlen - 1;
    REG32(SPI_MISO_DLEN) = bitlen - 1;
    REG32(SPI_USER1) = 0;

    // Start transfer
    REG32(SPI_CMD) = SPI_CMD_USR;
    while (REG32(SPI_CMD) & SPI_CMD_USR) asm volatile("nop");  // wait done

    // Read RX data from FIFO
    if (rx) {
        for (int i = 0; i < words; i++) {
            uint32_t w = wbuf[i];
            for (int b = 0; b < 4 && i * 4 + b < len; b++) {
                rx[i * 4 + b] = (uint8_t)(w >> (b * 8));
            }
        }
    }
}

// --- SPI2 transfer: single word (used for commands) ---
static void spi_transfer_word(uint32_t data, int bits) {
    volatile uint32_t *wbuf = (volatile uint32_t *)SPI_W0;
    wbuf[0] = data;

    REG32(SPI_USER) = SPI_USR_MOSI | SPI_USR_MISO;
    REG32(SPI_MOSI_DLEN) = bits - 1;
    REG32(SPI_MISO_DLEN) = 0;
    REG32(SPI_USER1) = 0;

    REG32(SPI_CMD) = SPI_CMD_USR;
    while (REG32(SPI_CMD) & SPI_CMD_USR) asm volatile("nop");
}

// --- Public API ---

void lcd_send_cmd(uint8_t cmd) {
    lcd_cs_low();
    lcd_dc_low();
    spi_transfer_word((uint32_t)cmd, 8);
    lcd_cs_high();
}

void lcd_send_data(uint8_t *data, size_t len) {
    lcd_cs_low();
    lcd_dc_high();
    // Send in 64-byte chunks (FIFO depth)
    int sent = 0;
    while (sent < (int)len) {
        int chunk = ((int)len - sent) > 64 ? 64 : ((int)len - sent);
        spi_transfer(data + sent, 0, chunk);
        sent += chunk;
    }
    lcd_cs_high();
}

void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    lcd_send_cmd(0x2A);  // column address set
    uint8_t col[] = { (uint8_t)(x >> 8), (uint8_t)(x & 0xFF),
                      (uint8_t)((x + w - 1) >> 8), (uint8_t)((x + w - 1) & 0xFF) };
    lcd_send_data(col, 4);

    lcd_send_cmd(0x2B);  // row address set
    uint8_t row[] = { (uint8_t)(y >> 8), (uint8_t)(y & 0xFF),
                      (uint8_t)((y + h - 1) >> 8), (uint8_t)((y + h - 1) & 0xFF) };
    lcd_send_data(row, 4);

    lcd_send_cmd(ILI_CMD_RAMWR);  // memory write
}

void lcd_write_pixels(uint16_t *pixels, size_t count) {
    lcd_dc_high();
    lcd_cs_low();
    int sent = 0;
    while (sent < (int)count) {
        int chunk = ((int)count - sent) > 32 ? 32 : ((int)count - sent);
        spi_transfer((const uint8_t *)(pixels + sent), 0, chunk * 2);
        sent += chunk;
    }
    lcd_cs_high();
}

void lcd_refresh(void) {
    framebuffer_t *f = fb_get();
    if (!f || !f->front) return;
    lcd_set_window(0, 0, GUI_LCD_W, GUI_LCD_H);
    lcd_write_pixels(f->front, (size_t)GUI_LCD_W * GUI_LCD_H);
}

// --- GPIO + SPI2 initialization ---
int lcd_init(void) {
    // Enable GPIO clock (use default, IO MUX is always on)

    // Configure LCD control pins as GPIO output
    int pins[] = { PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_CS };
    for (int i = 0; i < 3; i++) {
        int n = pins[i];
        // IO MUX: set as GPIO function (output)
        REG32(IO_MUX_GPIO_REG(n)) = (REG32(IO_MUX_GPIO_REG(n)) & ~0xFu) | IO_MUX_FUNC_GPIO;
        // GPIO matrix: output enable
        REG32(GPIO_ENABLE_REG) |= (1u << n);
        // Set default high
        REG32(GPIO_OUT_REG) |= (1u << n);
    }

    // Configure SPI pins (MOSI, MISO, SCLK) via IO MUX for FSPI function
    // IO MUX function 2 = FSPI (for these pins) [TRM 7.2]
    REG32(IO_MUX_GPIO_REG(PIN_LCD_MOSI)) = (REG32(IO_MUX_GPIO_REG(PIN_LCD_MOSI)) & ~0xFu) | IO_MUX_FUNC_FSPI;
    REG32(IO_MUX_GPIO_REG(PIN_LCD_MISO)) = (REG32(IO_MUX_GPIO_REG(PIN_LCD_MISO)) & ~0xFu) | IO_MUX_FUNC_FSPI;
    REG32(IO_MUX_GPIO_REG(PIN_LCD_SCLK)) = (REG32(IO_MUX_GPIO_REG(PIN_LCD_SCLK)) & ~0xFu) | IO_MUX_FUNC_FSPI;
    // Enable output on MOSI and SCLK via GPIO matrix
    REG32(GPIO_ENABLE_REG) |= (1u << PIN_LCD_MOSI) | (1u << PIN_LCD_SCLK);
    // MISO is input-only, no OE needed

    // --- SPI2 configuration [TRM 13.3] ---
    // Set clock: 40MHz / 40 = 1MHz SPI clock
    // CLK_EQU_SYSCLK=0, CLKCNT_N=39, CLKCNT_H=19, CLKCNT_L=19
    REG32(SPI_CLOCK) = (39 << SPI_CLKCNT_N_S) |
                       (19 << SPI_CLKCNT_H_S) |
                       (19 << SPI_CLKCNT_L_S);
    REG32(SPI_CLOCK) &= ~SPI_CLK_EQU_SYSCLK;  // use divider

    // Master mode: clear SLAVE bit
    REG32(SPI_SLAVE) = 0;

    // Set CS hold/setup
    REG32(SPI_USER) = SPI_CS_HOLD | SPI_CS_SETUP;

    // --- ILI9341 initialization sequence ---
    // Hardware reset
    REG32(GPIO_OUT_REG) &= ~(1u << PIN_LCD_RST);  // RST low
    delay_ms(10);
    REG32(GPIO_OUT_REG) |= (1u << PIN_LCD_RST);   // RST high
    delay_ms(120);

    // Software reset
    lcd_send_cmd(ILI_CMD_SWRESET);
    delay_ms(120);

    // Sleep out
    lcd_send_cmd(ILI_CMD_SLPOUT);
    delay_ms(150);

    // Pixel format: RGB565 (0x55)
    lcd_send_cmd(ILI_CMD_PIXFMT);
    uint8_t fmt = 0x55;
    lcd_send_data(&fmt, 1);

    // Memory access control: default orientation
    lcd_send_cmd(ILI_CMD_MADCTL);
    uint8_t madctl = 0x08;  // BGR order
    lcd_send_data(&madctl, 1);

    // Display on
    lcd_send_cmd(ILI_CMD_DISPON);
    delay_ms(50);

    return 0;
}