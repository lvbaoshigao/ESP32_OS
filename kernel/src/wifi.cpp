#include "wifi.h"
#include "rom_wifi.h"

// ============================================================
// ESP32-C6 Wi-Fi Driver — bare-metal ROM-based implementation
//
// Architecture (design concept from Linux mac80211, NOT copied):
//   - Hardware power/PHY state  !=  AP operational state.
//     mac80211 separates hw->start/stop (power) from
//     start_ap/stop_ap (interface). We mirror that:
//       g_wifi_powered : modem domain up + clocks running
//       g_wifi_ap_on    : SoftAP actively beaconing
//   - "active" therefore means AP broadcasting, not "driver loaded".
//     This is the honest semantic — an AP that transmits nothing
//     is not "active", it is "configured but dormant".
//
// Hardware facts (ESP32-C6):
//   - WDEV_BASE 0x600D0000 is RESERVED [TRM 5.3-2]. The 802.11
//     MAC/BB register file is NOT in the public peripheral map, so
//     all Wi-Fi control goes through ROM functions (rom_wifi.h).
//   - eFuse MAC read is pure register access [TRM 6.18/6.19] — safe.
//   - Modem domain power/clock is PMU+PCR register access [TRM 12.1,
//     8.61] — safe.
//   - PHY calibration + beaconing need the ROM net80211 path
//     (wdev_mac_wakeup / wdev_mac_reg_load), which is future work.
// ============================================================

// eFuse controller base [TRM 5.3-2] 0x600B0800; MAC lives in BLOCK1
#define EFUSE_BASE             0x600B0800u
#define EFUSE_RD_MAC_SPI_SYS_0 (EFUSE_BASE + 0x44)   // MAC[2..5] big-endian [TRM 6.18]
#define EFUSE_RD_MAC_SPI_SYS_1 (EFUSE_BASE + 0x48)   // MAC[0..1] in high16, MAC_EXT in low16 [TRM 6.19]

// Default MAC (used only if efuse read fails)
uint8_t g_wifi_mac[6] = {0x30, 0xAE, 0xA4, 0xC6, 0x01, 0x00};

// Read the real factory MAC from EFUSE_BLOCK1. Using registers (verified in
// TRM 6.18/6.19) avoids the risky ROM helper wifi_get_macaddr (0x40000ba0).
// Byte order verified on hw against esptool's BASE MAC f0:f5:bd:01:98:84:
//   reg0 = 0xBD019884  -> [BD][01][98][84] = MAC[2..5]
//   reg1 = 0xFFFEF0F5  -> low 16 bits [F0][F5] = MAC[0..1]; high 16 bits
//                          [FF][FE] are MAC_EXT (EUI-64 spacer), NOT part of the
//                          6-byte LAN MAC.
static void wifi_read_efuse_mac(uint8_t mac[6]) {
    uint32_t lo = REG32(EFUSE_RD_MAC_SPI_SYS_0);
    uint32_t hi = REG32(EFUSE_RD_MAC_SPI_SYS_1);

    if (lo == 0 && (hi & 0xFFFF) == 0) {
        // eFuse empty/unreadable — keep default
        klog("[WIFI] efuse MAC empty, using default\r\n");
        return;
    }

    // reg0 bytes -> MAC[2..5]; reg1 low 16 bits -> MAC[0..1].
    mac[0] = (uint8_t)((hi >> 8) & 0xFF);
    mac[1] = (uint8_t)(hi & 0xFF);
    mac[2] = (uint8_t)((lo >> 24) & 0xFF);
    mac[3] = (uint8_t)((lo >> 16) & 0xFF);
    mac[4] = (uint8_t)((lo >> 8) & 0xFF);
    mac[5] = (uint8_t)(lo & 0xFF);
}

// ---- Driver state ----
// g_wifi_powered: modem domain powered + APB clocked (hardware truth).
// g_wifi_ap_on:   SoftAP interface actively beaconing. This is what
//                 "wifi active" means to a user — a silent AP is not
//                 active, it is configured.
static int g_wifi_powered = 0;   // modem domain up + APB clocked [mac80211 op .start]
static int g_wifi_ap_on = 0;     // SoftAP beaconing [op .start_ap completed]
static int g_wifi_ap_started = 0; // AP configured (SSID/CH set) [op .start_ap called]
static int g_wifi_channel = 6;
static char g_wifi_ssid[33] = "ESP32-OS";
static char g_wifi_pass[32] = "";

// ---- PHY initialization sequence ----
// Reference: ESP-IDF esp_phy_init.c, esp_phy_enable() — concept only,
// the call list below is ordered per the ROM PHY API.

// Global "verbose PHY" flag: when set, each ROM PHY step is logged live to
// UART *before* the call, so if a ROM function traps/hangs the last printed
// step number pinpoints exactly which one. This is the B-M1 probe from
// AI/AI_project/think/01_wifi_scan_feasibility_en.md — these ROM addresses
// and their call order are cross-checked but NOT proven on this silicon, so
// the first hardware run must be observable.
static int g_phy_verbose = 0;

// One ordered PHY-init step. Kept in a table so the verbose probe can log a
// live "step N/-> addr" marker before entering the ROM, without duplicating
// the call site 30+ times.
struct phy_step_t { const char* tag; uint32_t addr; };

static const phy_step_t g_phy_steps[] = {
    {"rf_phy_enable",     ROM_WIFI_RF_PHY_ENABLE},
    {"mac_enable_bb",     ROM_MAC_ENABLE_BB},
    {"fe_reg_init",       ROM_FE_REG_INIT},
    {"bb_reg_init",       ROM_BB_REG_INIT},
    {"agc_reg_init",      ROM_AGC_REG_INIT},
    {"i2cmst_reg_init",   ROM_I2CMST_REG_INIT},
    {"pwdet_reg_init",    ROM_PWDET_REG_INIT},
    {"open_i2c_xpd",      ROM_OPEN_I2C_XPD},
    {"set_rxclk_en",      ROM_SET_RXCLK_EN},
    {"set_txclk_en",      ROM_SET_TXCLK_EN},
    {"fe_txrx_reset",     ROM_FE_TXRX_RESET},
    {"fe_adc_on",         ROM_FE_ADC_ON},
    {"bbpll_cal",         ROM_PHY_BBPLL_CAL},
    {"set_bbfreq_init",   ROM_PHY_SET_BBFREQ_INIT},
    {"ant_init",          ROM_PHY_ANT_INIT},
    {"bb_wdg_cfg",        ROM_BB_WDG_CFG},
    {"set_rx_comp",       ROM_SET_RX_COMP},
    {"gen_rx_gain_table", ROM_GEN_RX_GAIN_TABLE},
    {"enable_agc",        ROM_ENABLE_AGC},
    {"noise_floor_auto",  ROM_NOISE_FLOOR_AUTO},
    {"freq_correct",      ROM_PHY_FREQ_CORRECT},
    {"tx_pwctrl_init",    ROM_TX_PWCTRL_INIT},
    {"tx_pwctrl_cal",     ROM_TX_PWCTRL_INIT_CAL},
    {"rfcal_pwrctrl",     ROM_RFCAL_PWRCTRL},
    {"tx_cap_init",       ROM_TX_CAP_INIT},
    {"rfcal_txcap",       ROM_RFCAL_TXCAP},
    {"txiq_cal_init",     ROM_TXIQ_CAL_INIT},
    {"rfcal_txiq",        ROM_RFCAL_TXIQ},
    {"txdc_cal_init",     ROM_TXDC_CAL_INIT},
    {"txdc_cal",          ROM_TXDC_CAL},
    {"tx_paon_set",       ROM_TX_PAON_SET},
    {"phy_enable_cca",    ROM_PHY_ENABLE_CCA},
    {"iq_est_enable",     ROM_IQ_EST_ENABLE},
};
#define PHY_STEP_COUNT ((int)(sizeof(g_phy_steps)/sizeof(g_phy_steps[0])))

static int phy_init_sequence(void) {
    klog("[WIFI] PHY init begin\r\n");
    if (g_phy_verbose)
        kprintf("[WIFI] PHY init: %d ROM steps\r\n", PHY_STEP_COUNT);

    for (int i = 0; i < PHY_STEP_COUNT; i++) {
        if (g_phy_verbose)
            kprintf("[WIFI] step %d/%d -> %s @0x%08x\r\n",
                    i + 1, PHY_STEP_COUNT, g_phy_steps[i].tag, g_phy_steps[i].addr);
        rom_call_void(g_phy_steps[i].addr);
    }

    if (g_phy_verbose)
        kprintf("[WIFI] all %d PHY steps returned\r\n", PHY_STEP_COUNT);
    klog("[WIFI] PHY init complete\r\n");
    return 0;
}

// ---- Modem power domain ----

// Power up the Modem (Wi-Fi/BT radio) domain: required before ANY ROM
// PHY access. Equivalent to mac80211's "start powers the hardware".
// [TRM 12.1 PMU_HP_ACTIVE_DIG_POWER_REG] [TRM 8.61 PCR_MODEM_APB_CONF_REG]
static int modem_power_up(void) {
    // 1. Clear WIFI_PD (bit2) to power the radio domain.
    REG32(PMU_HP_ACTIVE_DIG_POWER) &= ~PMU_HP_ACTIVE_WIFI_PD;

    // 2. Enable the Modem APB clock (bit0) and de-assert reset (bit1).
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) |= MODEM_APB_CLK_EN;
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) &= ~MODEM_APB_RST;

    // Settle: clock/reset propagation.
    for (volatile int i = 0; i < 100; i++) asm volatile("");

    g_wifi_powered = 1;
    klog("[WIFI] modem power + clock enabled\r\n");
    return 0;
}

// Reverse of modem_power_up — for wifi_ap_stop / power-down.
static int modem_power_down(void) __attribute__((unused));
static int modem_power_down(void) {
    // Re-assert Modem reset, drop APB clock, power down domain.
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) |= MODEM_APB_RST;
    REG32(PCR_BASE + PCR_MODEM_APB_CONF) &= ~MODEM_APB_CLK_EN;
    REG32(PMU_HP_ACTIVE_DIG_POWER) |= PMU_HP_ACTIVE_WIFI_PD;

    g_wifi_powered = 0;
    klog("[WIFI] modem power down\r\n");
    return 0;
}

// ---- MAC address setup ----

static void wifi_set_mac_address(void) {
    // Prefer the real factory MAC read from eFuse registers (safe, no ROM call).
    wifi_read_efuse_mac(g_wifi_mac);

    kprintf("[WIFI] MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
            g_wifi_mac[0], g_wifi_mac[1], g_wifi_mac[2],
            g_wifi_mac[3], g_wifi_mac[4], g_wifi_mac[5]);
}

// ---- Driver interface ----

// mac80211 concept: start() = power the hardware, NOT bring up an AP.
static int wifi_drv_init(void) {
    kprintf("[WIFI] init start\r\n");
    klog("[WIFI] ESP32-C6 Wi-Fi driver init\r\n");

    // Power up the Modem domain + APB clock. Prerequisite for ANY ROM
    // PHY access; without it the radio is unclocked.
    modem_power_up();

    // Read MAC from efuse (safe register access, independent of WDEV).
    wifi_set_mac_address();

    kprintf("[WIFI] driver ready (PHY deferred)\r\n");
    klog("[WIFI] Wi-Fi driver ready (ROM PHY init deferred)\r\n");
    return 0;
}

static int wifi_drv_read(void* buf, int len) {
    (void)buf; (void)len;
    return -1; // Frame RX not yet implemented
}

static int wifi_drv_write(const void* buf, int len) {
    (void)buf; (void)len;
    return -1; // Frame TX not yet implemented
}

static int wifi_drv_ioctl(int cmd, void* arg) {
    (void)cmd; (void)arg;
    return -1;
}

driver_t g_wifi_driver = {
    "wifi", wifi_drv_init, wifi_drv_read, wifi_drv_write, wifi_drv_ioctl, 0
};

// ---- Public API ----

int wifi_init(void) {
    return wifi_drv_init();
}

// Full PHY calibration (call after boot for RF TX / scan / beacon).
// mac80211 concept: this is the "power up + calibrate" step that must
// complete before start_ap or hw_scan can transmit.
int wifi_phy_calibrate(void) {
    if (!g_wifi_powered) {
        klog("[WIFI] radio not powered — cannot calibrate\r\n");
        return -1;
    }
    klog("[WIFI] Full PHY calibration start\r\n");
    phy_init_sequence();
    klog("[WIFI] Full PHY calibration done\r\n");
    return 0;
}

// Verbose variant: logs every ROM PHY step live to UART. This is the B-M1
// hardware probe — run once via `wifi -cal` to see whether the ROM PHY path
// runs to completion on this silicon or traps at a specific step.
int wifi_phy_calibrate_verbose(void) {
    if (!g_wifi_powered) {
        uart_puts("[WIFI] radio not powered — cannot calibrate\r\n");
        return -1;
    }
    g_phy_verbose = 1;
    phy_init_sequence();
    g_phy_verbose = 0;
    return 0;
}

// ---- Noise-floor / channel-energy scan (feasibility doc Approach B) ----
//
// Read the hardware noise floor via the ROM. Both a wdev wrapper and a raw
// PHY reader exist; try the wdev wrapper first (it accounts for the current
// channel/AGC state), fall back to the raw reader.
//
// Signature note: on ESP32 ROM `read_hw_noisefloor` / `wdev_get_noise_floor`
// are `int f(void)` returning a signed dB value. This matches the
// rom_int_fn_t helper — no argument guessing involved.
int wifi_read_noisefloor(void) {
    // wdev_get_noise_floor is a thin wrapper around the PHY reader.
    return rom_call_int(ROM_WDEV_GET_NOISE_FLOOR);
}

// NOTE: a per-channel energy scan (feasibility doc Approach B) was attempted
// here but removed: set_channel_rfpll() resets the chip bare-metal exactly
// like the full PHY init (verified on hardware — see §6b of
// 01_wifi_scan_feasibility_en.md). No reset-safe scan path exists without
// ESP-IDF's phy_init_data, so no scan is wired.

// mac80211 concept: wifi_fw_loaded ~ "hardware powered" (start done).
int wifi_fw_loaded(void) {
    return g_wifi_powered;
}

// mac80211 concept: wifi_is_active ~ "AP started" (start_ap done).
// A configured-but-not-beaconing AP is NOT active.
int wifi_is_active(void) {
    return g_wifi_ap_on;
}

// AP configured (SSID/CH set) but not necessarily beaconing. Exposed for
// status reporting — mac80211 keeps "configured" separate from "up".
int wifi_ap_configured(void) {
    return g_wifi_ap_started;
}

// mac80211 concept: start_ap. Configures SSID/pass/channel and marks the
// SoftAP interface as up. Beacon transmission itself needs the ROM
// net80211 path (future work) — we report the true state.
int wifi_ap_start(const char* ssid, const char* password, int channel) {
    if (!g_wifi_powered) {
        klog("[WIFI] Radio not powered (modem domain down)\r\n");
        return -1;
    }

    if (ssid && ssid[0]) {
        k_strncpy(g_wifi_ssid, ssid, 32);
        g_wifi_ssid[32] = '\0';
    }
    if (password) {
        k_strncpy(g_wifi_pass, password, 31);
        g_wifi_pass[31] = '\0';
    }
    if (channel > 0 && channel <= 14)
        g_wifi_channel = channel;

    g_wifi_ap_started = 1;

    kprintf("[WIFI] AP configured (SSID=\"%s\", CH=%d)\r\n",
            g_wifi_ssid, g_wifi_channel);
    // g_wifi_ap_on stays 0: until the ROM net80211 TX path is wired and PHY
    // calibrated, no beacon is transmitted, so the AP is genuinely NOT active
    // on the air. wifi_is_active() therefore returns false (honest).
    klog("[WIFI] WARNING: no beacon on air — PHY not calibrated, ROM TX path not wired\r\n");
    return 0;
}

// mac80211 concept: stop_ap. Clears the AP config; the radio stays powered
// (mac80211 also keeps the HW up until op .stop).
int wifi_ap_stop(void) {
    g_wifi_ap_started = 0;
    g_wifi_ap_on = 0;
    klog("[WIFI] AP stopped (config only, radio still powered)\r\n");
    return 0;
}

// mac80211 concept: hw_scan. The ROM scan path (g_scan) requires a
// calibrated PHY + the net80211 stack. Until then the honest answer is
// "no APs discoverable".
int wifi_scan(struct wifi_ap_t* results, int max_results) {
    if (!results || max_results < 1) return 0;
    if (!g_wifi_powered) {
        uart_puts("Wi-Fi Scan: radio not powered\r\n");
        return 0;
    }
    // A real scan needs a calibrated PHY. Empirically the ESP32-C6 ROM RF/PHY
    // calls reset the chip when invoked bare-metal (no ESP-IDF phy_init_data),
    // so there is no reachable scan path. Honest answer: no APs discoverable.
    uart_puts("Wi-Fi Scan: unavailable (ROM PHY calibration resets the chip on\r\n"
              "bare-metal ESP32-C6; needs ESP-IDF phy_init_data)\r\n");
    return 0;
}

// mac80211 concept: get_stats / config. Returns configured state.
int wifi_get_ap_info(struct wifi_ap_t* info) {
    if (!info) return -1;
    k_memset(info, 0, sizeof(*info));

    k_memcpy(info->ssid, g_wifi_ssid, k_strlen(g_wifi_ssid));
    info->ssid_len = k_strlen(g_wifi_ssid);
    k_memcpy(info->bssid, g_wifi_mac, 6);
    info->channel = (uint8_t)g_wifi_channel;
    info->encryption = WIFI_ENC_OPEN;
    if (g_wifi_pass[0]) info->encryption = WIFI_ENC_WPA2;
    return 0;
}
