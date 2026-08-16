# ESP32-C6 Register Definitions for Bootloader
# All addresses verified against ESP32-C6 TRM v1.2
# Section references: [TRM chapter.section]

# === Memory Map [TRM 5.3-1] ===
.equ HP_SRAM_START,     0x40800000      # HP SRAM base (512KB)
.equ HP_SRAM_END,       0x40880000      # HP SRAM end (exclusive)
.equ HP_SRAM_SIZE,      0x00080000      # 512KB

# === Stack ===
.equ STACK_TOP,         HP_SRAM_END     # SP starts at top of HP SRAM

# === UART0 [TRM 27.6.1] ===
.equ UART0_BASE,        0x60000000
.equ UART_FIFO_REG,     0x0000          # TX/RX FIFO (write=TX, read=RX)
.equ UART_CLKDIV_REG,   0x0014          # UART_CLKDIV_SYNC_REG
.equ UART_STATUS_REG,   0x001C
.equ UART_CONF0_REG,    0x0020          # UART_CONF0_SYNC_REG
.equ UART_CLK_CONF_REG, 0x0088
.equ UART_REG_UPDATE,   0x0098

# UART_STATUS_REG fields [TRM 27.21]
.equ UART_TXFIFO_CNT_SHIFT, 16
.equ UART_TXFIFO_CNT_MASK,  0xFF
.equ UART_TX_FIFO_SIZE,     128
# UART_STATUS_REG RX fields
.equ UART_RXFIFO_CNT_MASK,  0xFF        # bits[7:0]
# UART_STATUS_REG TX fields
.equ UART_TX_IDLE_BIT,  (1 << 12)       # bit[12]: TX_IDLE

# UART_CONF0_SYNC_REG fields [TRM 27.5]
# 8N1: BIT_NUM=3(8bit) at [3:2], STOP_BIT_NUM=1 at [5:4], parity off
.equ UART_CONF0_8N1,    0x1C            # (1<<4)|(3<<2) = 0x1C
.equ UART_TXFIFO_RST,   (1 << 21)
.equ UART_RXFIFO_RST,   (1 << 22)

# UART_CLKDIV for 115200 baud from 40MHz XTAL
# 40000000/115200 = 347.222 -> int=347(0x15B), frac=4(0.25)
.equ UART_CLKDIV_115200, ((4 << 20) | 347)

# UART_CLK_CONF_REG fields [TRM 27.6]
.equ UART_TX_SCLK_EN,   (1 << 25)
.equ UART_RX_SCLK_EN,   (1 << 24)

# UART_REG_UPDATE bit
.equ UART_REG_UPDATE_BIT, (1 << 0)

# === PCR (Peripheral Clock/Reset) [TRM 8.1-8.3] ===
.equ PCR_BASE,           0x60096000
.equ PCR_UART0_CONF,     0x0000         # bit0=CLK_EN, bit1=RST_EN
.equ PCR_UART0_SCLK_CONF,0x0004         # bit[21:20]=SCLK_SEL, bit22=SCLK_EN
# SCLK_SEL values: 1=PLL_F80M, 2=RC_FAST, 3=XTAL(default)
.equ PCR_SCLK_SEL_XTAL,  (3 << 20)
.equ PCR_SCLK_EN,        (1 << 22)

# === Direct Boot Magic [TRM 9.2] ===
.equ DIRECT_BOOT_MAGIC,  0xaedb041d

# === Watchdog Timers [TRM 14, 15] ===
.equ WDT_UNLOCK_KEY,     0x50D83AA1

# MWDT0 (Timer Group 0) [TRM 14]
# ESP32-C6 has only ONE Timer Group (TG0). TIMG1 does not exist on C6.
.equ TIMG0_BASE,          0x60008000
.equ TIMG_WDTCONFIG0,     0x0048
.equ TIMG_WDTWPROTECT,    0x0064
.equ TIMG_WDT_CONF_UPDATE, (1 << 21)

# RWDT (RTC/LP WDT) [TRM 15]
.equ RWDT_BASE,           0x600B1C00
.equ RWDT_CONFIG0,        0x0000
.equ RWDT_WPROTECT,       0x0018

# SWD (Super Watchdog) [TRM 15]
.equ SWD_CONFIG,          0x001C
.equ SWD_WPROTECT,        0x0020
.equ SWD_DISABLE_BIT,     (1 << 30)

# TWDT (Trusted WDT) [TRM 15.x] — part of TEE block
.equ TEE_BASE,            0x600B2000
.equ TWDT_CONF,           0x0000
.equ TWDT_WPROTECT,       0x0024

# === GPIO [TRM 6] ===
.equ GPIO_BASE,           0x60091000     # DR_REG_GPIO_BASE (0x60090000 is IO_MUX!)
.equ GPIO_ENABLE_REG,     0x0020         # GPIO_ENABLE_OUT_REG
.equ GPIO_ENABLE_W1TS,    0x0024         # write-1-to-set (does not clobber other pins)

# === IO_MUX [TRM 6.2] ===
# Verified against ESP-IDF components/soc/esp32c6/register/soc/io_mux_reg.h
.equ IO_MUX_BASE,         0x60090000     # DR_REG_IO_MUX_BASE
# Per-pin register: NOT base+pin*4. GPIO0 is at base+0x04, so pin N = base + 4 + N*4
.equ IO_MUX_GPIO16,       0x44           # PERIPHS_IO_MUX_U0TXD_U
.equ IO_MUX_GPIO17,       0x48           # PERIPHS_IO_MUX_U0RXD_U
# MCU_SEL is bits [14:12]. Function 0 = U0TXD/U0RXD (1 = plain GPIO, 2 = FSPICS)
.equ IO_MUX_SEL_UART0,    (0 << 12)      # FUNC_U0TXD_U0TXD / FUNC_U0RXD_U0RXD
.equ IO_MUX_FUN_IE,       (1 << 9)       # FUN_IE: input enable
.equ IO_MUX_FUN_PU,       (1 << 8)       # FUN_PU: pull-up (bit 7 is FUN_PD — do not set)

# === SHA Accelerator [TRM 23] ===
.equ SHA_BASE,            0x60089000
.equ SHA_MODE_REG,        0x0000        # bits[2:0]: 0=SHA1, 1=SHA224, 2=SHA256
.equ SHA_START_REG,       0x0010        # write 1 to start first block
.equ SHA_BUSY_REG,        0x0018        # bit0: 1=busy
.equ SHA_H_0_REG,         0x0040        # digest word 0 (MSW)
.equ SHA_M_0_REG,         0x0080        # message word 0
.equ SHA_MODE_256,        2
.equ SHA_DIGEST_WORDS,    8             # 8 x 32-bit = 256 bits

# PCR for SHA [TRM 8]
.equ PCR_SHA_CONF,        0x00CC        # bit0=CLK_EN, bit1=RST_EN

# === SPI1 Flash Controller [TRM 28 / MSPI] ===
.equ SPI1_BASE,           0x60002000
.equ SPI1_CMD_REG,        0x0000        # bit18=SPI_USR (trigger)
.equ SPI1_ADDR_REG,       0x0004        # flash addr, MSB-justified for 24-bit
.equ SPI1_USER_REG,       0x0010        # phase enables
.equ SPI1_USER1_REG,      0x0014        # addr/dummy bit lengths
.equ SPI1_USER2_REG,      0x0018        # command value + bit length
.equ SPI1_MS_DLEN_REG,    0x001C        # data bit length (bits-1)
.equ SPI1_W0_REG,         0x0098        # data buffer word 0

.equ SPI_USR_BIT,         (1 << 18)
.equ SPI_USR_COMMAND,     (1 << 31)
.equ SPI_USR_ADDR,        (1 << 30)
.equ SPI_USR_MISO,        (1 << 28)
.equ SPI_USR_MOSI,        (1 << 27)
.equ SPI_ADDR_BITLEN_23,  (23 << 27)    # 24-bit address
.equ SPI_CMD_BITLEN_7,    (7 << 28)     # 8-bit command

# Flash commands
.equ FLASH_CMD_WREN,      0x06
.equ FLASH_CMD_RDSR,      0x05
.equ FLASH_CMD_WRSR,      0x01          # Write Status Register
.equ FLASH_CMD_PP,        0x02          # Page Program
.equ FLASH_CMD_SE,        0x20          # Sector Erase 4KB
# end of reg_c6.inc
