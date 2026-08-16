#include "gui.h"
#include "kernel.h"

// === I2C Touch driver — ESP32-C6 I2C0 register-level, FT5x06 ===
// References: ESP32-C6 TRM Chapters 18 (I2C), 6 (GPIO), 7 (IO MUX)

// --- ESP32-C6 peripheral base addresses [TRM 2.4] ---
#define GPIO_BASE       0x60040400u
#define IO_MUX_BASE     0x60090800u
#define I2C0_BASE       0x60053000u

// --- GPIO registers [TRM 6] ---
#define GPIO_ENABLE_REG     (GPIO_BASE + 0x0C)
#define GPIO_OUT_REG        (GPIO_BASE + 0x04)

// --- IO MUX registers [TRM 7] ---
#define IO_MUX_GPIO_REG(n)  (IO_MUX_BASE + 0x20 + (n) * 4)
#define IO_MUX_FUNC_GPIO    0

// --- I2C0 registers [TRM 18] — ESP32-C3/C6 layout ---
#define I2C_CMD             (I2C0_BASE + 0x000)  // [TRM 18.4.1]
#define I2C_ADDR            (I2C0_BASE + 0x004)  // [TRM 18.4.2]
#define I2C_CONF            (I2C0_BASE + 0x00C)  // [TRM 18.4.4]
#define I2C_FIFO            (I2C0_BASE + 0x018)  // [TRM 18.4.7]
#define I2C_SCL_LOW         (I2C0_BASE + 0x01C)  // [TRM 18.4.8]
#define I2C_SCL_HIGH        (I2C0_BASE + 0x020)  // [TRM 18.4.9]
#define I2C_SDA_HOLD        (I2C0_BASE + 0x024)  // [TRM 18.4.10]
#define I2C_SDA_SAMPLE      (I2C0_BASE + 0x028)  // [TRM 18.4.11]
#define I2C_FIFO_CONF       (I2C0_BASE + 0x02C)  // [TRM 18.4.12]
#define I2C_FIFO_ST         (I2C0_BASE + 0x030)  // [TRM 18.4.13]
#define I2C_TOUT            (I2C0_BASE + 0x034)  // [TRM 18.4.14]

// I2C_CMD bits
#define I2C_CMD_START       (1u << 0)
#define I2C_CMD_STOP        (1u << 1)
#define I2C_CMD_READ        (1u << 2)
#define I2C_CMD_TRIGGER     (1u << 15)
#define I2C_CMD_DONE        (1u << 15)  // bit 15 clears when done

// I2C_CONF bits [TRM 18.4.4]
#define I2C_MS_MODE         (1u << 7)   // 1 = master
#define I2C_SLAVE_EN        (1u << 0)   // 0 = master mode

// I2C_FIFO_CONF bits [TRM 18.4.12]
#define I2C_TX_FIFO_RST     (1u << 1)
#define I2C_RX_FIFO_RST     (1u << 0)

// I2C_FIFO_ST bits [TRM 18.4.13]
#define I2C_RX_CNT_M        0x1F
#define I2C_TX_CNT_S        6
#define I2C_TX_CNT_M        (0x1F << 6)

// FT5x06 constants
#define FT5x06_ADDR         0x38        // 7-bit I2C address
#define FT5x06_REG_DATA     0x00        // first data register
#define FT5x06_REG_TD_STAT  0x02        // touch point count
#define FT5x06_TOUCH_DATA_LEN 14        // registers 0x00-0x0D

// --- Pin assignments ---
#define PIN_TOUCH_SDA       1
#define PIN_TOUCH_SCL       2
#define PIN_TOUCH_RST       4

// Timeout
#define I2C_TIMEOUT_US      50000

// --- I2C transaction helpers ---
// ESP32-C6 I2C master: write data to FIFO, toggle trigger, poll done

static void i2c_reset_fifo(void) {
    REG32(I2C_FIFO_CONF) = I2C_TX_FIFO_RST | I2C_RX_FIFO_RST;
    REG32(I2C_FIFO_CONF) = 0;
}

// Wait for I2C_CMD bit 15 to clear (transaction done)
static int i2c_wait_done(void) {
    uint32_t start = uptime_us();
    while (REG32(I2C_CMD) & I2C_CMD_DONE) {
        if (uptime_us() - start > I2C_TIMEOUT_US) return -1;
        asm volatile("nop");
    }
    return 0;
}

// Write N bytes to slave at given register address
static int i2c_write(uint8_t slave_addr, uint8_t reg, const uint8_t *data, int len) __attribute__((unused));
static int i2c_write(uint8_t slave_addr, uint8_t reg, const uint8_t *data, int len) {
    i2c_reset_fifo();
    // Write phase: slave addr + W, register address, then data bytes
    REG32(I2C_FIFO) = slave_addr << 1;  // device + W
    REG32(I2C_FIFO) = reg;
    for (int i = 0; i < len; i++)
        REG32(I2C_FIFO) = data[i];
    // Set slave address, no stop, trigger
    REG32(I2C_ADDR) = slave_addr;
    REG32(I2C_CMD) = I2C_CMD_START | I2C_CMD_STOP | I2C_CMD_TRIGGER;
    return i2c_wait_done();
}

// Read N bytes from slave at given register address
static int i2c_read(uint8_t slave_addr, uint8_t reg, uint8_t *buf, int len) {
    i2c_reset_fifo();

    // Write phase: send slave addr + W + register address
    REG32(I2C_FIFO) = slave_addr << 1;
    REG32(I2C_FIFO) = reg;
    REG32(I2C_ADDR) = slave_addr;
    REG32(I2C_CMD) = I2C_CMD_START | I2C_CMD_TRIGGER;  // no stop yet
    if (i2c_wait_done() != 0) return -1;

    // Read phase: repeated start, slave addr + R, read len bytes
    i2c_reset_fifo();
    // For read: set number of bytes to read in read command
    // Put slave addr + R in FIFO, then trigger read
    REG32(I2C_FIFO) = (slave_addr << 1) | 1;
    REG32(I2C_ADDR) = slave_addr;
    // Configure read length
    REG32(I2C_CONF) = I2C_MS_MODE | ((len - 1) << 1);  // read count in bits 1-5
    REG32(I2C_CMD) = I2C_CMD_START | I2C_CMD_STOP | I2C_CMD_READ | I2C_CMD_TRIGGER;
    if (i2c_wait_done() != 0) return -1;

    // Read from FIFO
    for (int i = 0; i < len; i++) {
        buf[i] = (uint8_t)(REG32(I2C_FIFO) & 0xFF);
    }
    return 0;
}

// --- Touch driver API ---

int touch_init(void) {
    // Configure I2C pins as GPIO
    int pins[] = { PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_RST };
    for (int i = 0; i < 3; i++) {
        int n = pins[i];
        REG32(IO_MUX_GPIO_REG(n)) = (REG32(IO_MUX_GPIO_REG(n)) & ~0xFu) | IO_MUX_FUNC_GPIO;
        REG32(GPIO_ENABLE_REG) |= (1u << n);
        REG32(GPIO_OUT_REG) |= (1u << n);  // default high
    }

    // Reset touch controller via RST pin
    REG32(GPIO_OUT_REG) &= ~(1u << PIN_TOUCH_RST);
    uint32_t t0 = uptime_us();
    while (uptime_us() - t0 < 1000) asm volatile("nop");  // ~1ms
    REG32(GPIO_OUT_REG) |= (1u << PIN_TOUCH_RST);
    t0 = uptime_us();
    while (uptime_us() - t0 < 5000) asm volatile("nop");  // ~5ms

    // --- I2C0 master configuration [TRM 18.3] ---
    REG32(I2C_CONF) = I2C_MS_MODE;  // master, not slave

    // 400kHz from 40MHz APB: SCL_LOW = SCL_HIGH = 49
    REG32(I2C_SCL_LOW) = 49;
    REG32(I2C_SCL_HIGH) = 49;
    REG32(I2C_SDA_HOLD) = 10;
    REG32(I2C_SDA_SAMPLE) = 10;
    REG32(I2C_TOUT) = 0;  // disable timeout

    // Map I2C0 signals to GPIO via IO MUX (function 1 = I2C)
    REG32(IO_MUX_GPIO_REG(PIN_TOUCH_SDA)) = (REG32(IO_MUX_GPIO_REG(PIN_TOUCH_SDA)) & ~0xFu) | 1;
    REG32(IO_MUX_GPIO_REG(PIN_TOUCH_SCL)) = (REG32(IO_MUX_GPIO_REG(PIN_TOUCH_SCL)) & ~0xFu) | 1;

    i2c_reset_fifo();

    // Verify FT5x06: read device mode register
    uint8_t id = 0;
    if (i2c_read(FT5x06_ADDR, 0x00, &id, 1) == 0) {
        return 0;  // device responded
    }
    return -1;
}

int touch_scan(touch_point_t *pts, int max) {
    if (!pts || max <= 0) return 0;

    uint8_t buf[FT5x06_TOUCH_DATA_LEN];
    if (i2c_read(FT5x06_ADDR, FT5x06_REG_DATA, buf, FT5x06_TOUCH_DATA_LEN) != 0) {
        return 0;
    }

    int td_status = buf[FT5x06_REG_TD_STAT];
    if (td_status > 2) td_status = 2;

    int count = 0;
    for (int i = 0; i < td_status && i < max; i++) {
        int off = 3 + i * 6;
        uint8_t xh = buf[off];
        uint8_t xl = buf[off + 1];
        uint8_t yh = buf[off + 2];
        uint8_t yl = buf[off + 3];
        pts[count].x = (uint16_t)(((uint16_t)(xh & 0x0F) << 8) | xl);
        pts[count].y = (uint16_t)(((uint16_t)(yh & 0x0F) << 8) | yl);
        pts[count].pressure = buf[off + 5];
        pts[count].id = (uint8_t)i;
        count++;
    }
    return count;
}