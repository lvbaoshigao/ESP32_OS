#include "wifi.h"
#include "rom_wifi.h"

// ============================================================
// ESP32-C6 Bluetooth LE Driver — bare-metal ROM-based
// Uses on-chip ROM functions for BT PHY init.
//
// State model (design concept from the Linux Bluetooth stack —
// hci_dev register/open vs mgmt_start_discovery, NOT copied):
//   - g_bt_powered : radio clocked + modem domain up (HCI "open")
//   - g_bt_scanning : a discovery is in progress
// "Powered" does not imply "scanning" — keep them distinct.
// ponytail: scan-only initially. Add GATT/connection when needed.
// ============================================================

static int g_bt_powered  = 0;   // BT clock + modem power up
static int g_bt_scanning = 0;   // discovery in progress

// BT clock control via PCR
static void bt_clock_enable(void) {
    // 1. Power up the Modem domain (Wi-Fi/BT radio). HP_ACTIVE state,
    //    bit2 = WIFI_PD; 0 = power up. [TRM 12.1]
    REG32(PMU_HP_ACTIVE_DIG_POWER) &= ~PMU_HP_ACTIVE_WIFI_PD;

    // 2. Enable the Modem APB clock (needed for both Wi-Fi MAC and BT).
    //    bit0 = CLK_EN, bit1 = RST_EN. [TRM 8.61]
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) |= MODEM_APB_CLK_EN;

    // 3. De-assert the Modem reset once clocks are up.
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) &= ~MODEM_APB_RST;
    for (volatile int i = 0; i < 100; i++) asm volatile("");
    klog("[BT] BT clock + modem power enabled\r\n");
}

static void bt_clock_disable(void) __attribute__((unused));
static void bt_clock_disable(void) {
    // Re-assert Modem reset, drop APB clock.
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) |= MODEM_APB_RST;
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) &= ~MODEM_APB_CLK_EN;
}

// ---- BT PHY initialization via ROM functions ----

static int bt_phy_init(void) __attribute__((unused));
static int bt_phy_init(void) {
    klog("[BT] BT PHY init begin\r\n");

    // BT-specific calibration
    rom_call_void(ROM_BT_TXDC_CAL);
    rom_call_void(ROM_BT_TXIQ_CAL);
    rom_call_void(ROM_BT_TX_PWCTRL_INIT);

    // BT filter register config
    rom_call_void(ROM_BT_FILTER_REG);

    // BT gain offset
    rom_call_void(ROM_BT_GAIN_OFFSET);

    // BT antenna config
    rom_call_void(ROM_ANT_BTTX_CFG);
    rom_call_void(ROM_ANT_BTRX_CFG);

    klog("[BT] BT PHY init complete\r\n");
    return 0;
}

// ---- BT MAC address ----
// Use same MAC as Wi-Fi with different OUI or offset
static uint8_t g_bt_mac[6] = {0x30, 0xAE, 0xA4, 0xC6, 0x01, 0x01};

static void bt_set_mac(void) {
    // Derive BT MAC from the real Wi-Fi MAC (from efuse) + 1.
    // NOTE: WDEV_BT_ADDR writes removed — 0x600D0000 is reserved on ESP32-C6,
    // so they reached no hardware. The BT address is exposed for reporting only.
    for (int i = 0; i < 6; i++) g_bt_mac[i] = g_wifi_mac[i];
    g_bt_mac[5]++;  // BT addr = Wi-Fi MAC + 1

    kprintf("[BT] MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
            g_bt_mac[0], g_bt_mac[1], g_bt_mac[2],
            g_bt_mac[3], g_bt_mac[4], g_bt_mac[5]);
}

// ---- Driver interface ----

static int bt_drv_init(void) {
    klog("[BT] BLE driver init\r\n");

    bt_clock_enable();
    bt_set_mac();

    g_bt_powered = 1;
    klog("[BT] BLE driver ready (RF init deferred)\r\n");
    return 0;
}

static int bt_drv_read(void* buf, int len) {
    (void)buf; (void)len;
    return -1;  // HCI RX not yet implemented
}

static int bt_drv_write(const void* buf, int len) {
    (void)buf; (void)len;
    return -1;  // HCI TX not yet implemented
}

static int bt_drv_ioctl(int cmd, void* arg) {
    (void)cmd; (void)arg;
    return -1;
}

driver_t g_bt_driver = {
    "bluetooth", bt_drv_init, bt_drv_read, bt_drv_write, bt_drv_ioctl, 0
};

// ---- Public API ----

int bt_init(void) {
    return bt_drv_init();
}

// "Active" = radio powered + clocked (HCI device open). Distinct from
// scanning — BlueZ keeps "powered" and "discovering" separate.
int bt_is_active(void) {
    return g_bt_powered;
}

// mac80211/BlueZ concept: a discovery in progress. Reports whether a scan
// is currently running (not whether the radio is merely powered).
int bt_is_scanning(void) {
    return g_bt_scanning;
}

int bt_scan(struct bt_device_t* results, int max_results) {
    if (!results || max_results < 1) return 0;
    if (!g_bt_powered) {
        uart_puts("[BT] radio not powered — cannot scan\r\n");
        return 0;
    }

    // NOTE: WDEV_BT_SCAN_CTRL/COUNT/RESULT writes removed — 0x600D0000 is
    // reserved on ESP32-C6, so a real BLE scan needs the ROM HCI path (future
    // work). Honest answer: no devices until that path is wired.
    g_bt_scanning = 1;
    uart_puts("[BT] Scan not supported yet (ROM HCI path not wired)\r\n");
    g_bt_scanning = 0;
    return 0;
}