#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "kernel.h"

// ============================================================
// ESP32-C6 Wireless Device (WDEV) Driver
// Controls Wi-Fi (802.11ax) + Bluetooth LE + 802.15.4
// ============================================================

// === WDEV Subsystem Base Address [TRM 5.3-2] ===
#define WDEV_BASE           0x600D0000u

// === Register access macros ===
#define WDEV_REG(offset) (*(volatile uint32_t*)(WDEV_BASE + (offset)))
#define PCR_BASE 0x60096000u
#define PCR_REG(offset) (*(volatile uint32_t*)(PCR_BASE + (offset)))

// === PMU (Low-power Management) [TRM 12] ===
// Power up the Modem (Wi-Fi/BT radio) domain, which the ROM PHY code needs.
#define PMU_BASE                0x600B0000u
// bit0=VDD_SPI, bit1=HP_MEM_DSLP, bit2=WIFI_PD, bit3=CPU_PD, bit4=AON_PD, bit5=TOP_PD
#define PMU_HP_ACTIVE_DIG_POWER (PMU_BASE + 0x0000)
#define PMU_HP_ACTIVE_WIFI_PD   (1u << 2)
// MODEM_APB clock: bit0=CLK_EN, bit1=RST_EN (reset value 0x1 = CLK_EN set)
#define PCR_MODEM_APB_CONF      0x0108u
#define MODEM_APB_CLK_EN        (1u << 0)
#define MODEM_APB_RST           (1u << 1)

// === WDEV Clock and Reset (via PCR) ===
// PCR (Peripheral Clock Reset) at 0x60096000
#define PCR_WDEV_CONF       0x00E0          // bit0=CLK_EN, bit1=RST_EN
#define PCR_BT_CONF         0x00E4          // bit0=CLK_EN, bit1=RST_EN

// === WDEV Register Offsets (relative to WDEV_BASE) ===
#define WDEV_RF_CFG         0x0000          // RF configuration
#define WDEV_MAC_CTRL       0x0010          // MAC control
#define WDEV_MAC_STATUS     0x0014          // MAC status
#define WDEV_BB_CTRL        0x0020          // Baseband control
#define WDEV_INT_ST         0x0030          // Interrupt status
#define WDEV_INT_ENA        0x0034          // Interrupt enable
#define WDEV_FW_ADDR        0x0040          // Firmware load address
#define WDEV_FW_DATA        0x0044          // Firmware load data
#define WDEV_FW_CTRL        0x0048          // Firmware control (start/stop)
#define WDEV_FW_STATUS      0x004C          // Firmware status

// === MAC Configuration Registers ===
#define WDEV_MAC_ADDR_L     0x0100          // MAC address low 32 bits
#define WDEV_MAC_ADDR_H     0x0104          // MAC address high 16 bits
#define WDEV_OP_MODE        0x0110          // Operation mode (AP/STA/IBSS)
#define WDEV_BSSID          0x0114          // BSSID (for AP mode, same as MAC)
#define WDEV_CHANNEL        0x0118          // Current channel (1-14)
#define WDEV_TX_POWER       0x011C          // TX power in dBm

// === Beacon/Frame Configuration ===
#define WDEV_BEACON_INT     0x0120          // Beacon interval (TU, 1TU=1024us)
#define WDEV_BEACON_CTRL    0x0124          // Beacon control (enable/disable)
#define WDEV_DTIM_PERIOD    0x0128          // DTIM period
#define WDEV_SSID_LEN       0x012C          // SSID length
#define WDEV_SSID_DATA      0x0130          // SSID data (up to 32 bytes)

// === TX/RX Control ===
#define WDEV_TX_CTRL        0x0200          // TX control
#define WDEV_TX_STATUS      0x0204          // TX status
#define WDEV_RX_CTRL        0x0208          // RX control
#define WDEV_RX_STATUS      0x020C          // RX status
#define WDEV_RX_FRAME_CNT   0x0210          // Received frame count
#define WDEV_TX_FRAME_CNT   0x0214          // Transmitted frame count

// === Scan Control ===
#define WDEV_SCAN_CTRL      0x0300          // Scan control
#define WDEV_SCAN_STATUS    0x0304          // Scan status
#define WDEV_SCAN_RESULT    0x0308          // Scan result pointer
#define WDEV_SCAN_COUNT     0x030C          // Number of scan results

// === Bluetooth LE Registers ===
#define WDEV_BT_CTRL        0x0400          // BT control
#define WDEV_BT_STATUS      0x0404          // BT status
#define WDEV_BT_ADDR_L      0x0408          // BT address low
#define WDEV_BT_ADDR_H      0x040C          // BT address high
#define WDEV_BT_SCAN_CTRL   0x0410          // BT scan control
#define WDEV_BT_SCAN_RESULT 0x0414          // BT scan result
#define WDEV_BT_SCAN_COUNT  0x0418          // BT scan count

// === Operation modes ===
#define WDEV_MODE_STA       0               // Station mode
#define WDEV_MODE_AP        1               // SoftAP mode
#define WDEV_MODE_APSTA     2               // AP + Station

// === Encryption types for scan results ===
#define WIFI_ENC_OPEN       0
#define WIFI_ENC_WEP        1
#define WIFI_ENC_WPA        2
#define WIFI_ENC_WPA2       3
#define WIFI_ENC_WPA3       4
#define WIFI_ENC_UNKNOWN    5

// === Wi-Fi scan result ===
struct wifi_ap_t {
    uint8_t bssid[6];
    uint8_t ssid[32];
    int ssid_len;
    uint8_t channel;
    int8_t rssi;           // dBm
    uint8_t encryption;    // wifi_enc_t
};

#define MAX_SCAN_RESULTS 20

// === Bluetooth scan result ===
struct bt_device_t {
    uint8_t addr[6];
    char name[32];
    int8_t rssi;
};

#define MAX_BT_RESULTS 10

// === Public API ===
int wifi_init(void);                    // Initialize Wi-Fi subsystem (power + clock)
int wifi_wdev_ready(void);              // radio power + clock gated access gate
int wifi_phy_calibrate(void);           // full PHY calibration (precondition for TX)
int wifi_phy_calibrate_verbose(void);   // same, with live per-step UART logging (HW probe)
int wifi_read_noisefloor(void);         // read HW noise floor via ROM (signed dB)
int wifi_ap_start(const char* ssid, const char* password, int channel);
int wifi_ap_stop(void);
int wifi_ap_configured(void);           // AP interface configured (SSID/CH)
int wifi_scan(struct wifi_ap_t* results, int max_results);
int wifi_get_ap_info(struct wifi_ap_t* info);
int wifi_is_active(void);               // true only when AP is actually beaconing
int wifi_fw_loaded(void);               // true when radio is powered

int bt_init(void);                      // Initialize Bluetooth (power + clock)
int bt_scan(struct bt_device_t* results, int max_results);
int bt_is_active(void);                 // true when radio is powered
int bt_is_scanning(void);               // true when a discovery is in progress

// === MAC address (from efuse or default) ===
extern uint8_t g_wifi_mac[6];

#ifdef __cplusplus
}
#endif

#endif // WIFI_H