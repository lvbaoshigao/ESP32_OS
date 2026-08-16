#ifndef ROM_WIFI_H
#define ROM_WIFI_H
#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 ROM function addresses for Wi-Fi/BT PHY & MAC
// All addresses from esp32c6.rom.{phy,net80211,pp}.ld
// DO NOT EDIT — verified against ESP-IDF v6.0.2 ROM LD files
// ============================================================

// ---- PHY ROM functions (esp32c6.rom.phy.ld) ----

typedef int (*rom_phy_func_t)(void);
typedef struct {
    uint32_t version;
    void*    init_data;
    void*    (*get_init_data)(void);
    void     (*enable)(int modem);
    void     (*disable)(int modem);
    void     (*load_cal_and_init)(void);
    void     (*common_clock_enable)(void);
    void     (*common_clock_disable)(void);
    int      (*set_channel)(int channel);
    void     (*wifi_power_domain_on)(void);
    void     (*wifi_power_domain_off)(void);
    void     (*bt_power_domain_on)(void);
    void     (*bt_power_domain_off)(void);
} rom_phy_funcs_t;

// PHY function addresses
#define ROM_PHY_PARAM_ADDR      0x40001104
#define ROM_PHY_GET_ROMFUNCS    0x40001108
#define ROM_CHIP761_PHYROM_VER  0x4000110c  // returns version
#define ROM_SET_CHAN_CAL_INTERP 0x40001124
#define ROM_BB_BSS_CBW40        0x4000112C
#define ROM_SET_CHAN_REG        0x4000113C
#define ROM_SET_CHAN_FREQ_SW    0x40001144
#define ROM_FREQ_MODULE_RESETN  0x40001148
#define ROM_FREQ_CHAN_EN_SW     0x4000114C
#define ROM_WRITE_CHAN_FREQ     0x40001150
#define ROM_PHY_EN_HW_SET_FREQ  0x40001188
#define ROM_PHY_DIS_HW_SET_FREQ 0x4000118C
#define ROM_TX_PWCTRL_BG_INIT   0x400011D0
#define ROM_PHY_PWDET_ALWAYS_EN 0x400011D4
#define ROM_PHY_PWDET_ONETIME   0x400011D8
#define ROM_ANT_DFT_CFG         0x400011E0
#define ROM_ANT_WIFITX_CFG      0x400011E4
#define ROM_ANT_WIFIRX_CFG      0x400011E8
#define ROM_ANT_BTTX_CFG        0x400011EC
#define ROM_ANT_BTRX_CFG        0x400011F0
#define ROM_PHY_CHAN_DUMP_CFG   0x400011F4
#define ROM_PHY_ENABLE_LOW_RATE 0x400011F8
#define ROM_PHY_DISABLE_LOW_RATE 0x400011FC
#define ROM_PHY_IS_LOW_RATE_EN  0x40001200
#define ROM_PHY_DIG_REG_BACKUP  0x40001204
#define ROM_PHY_CHAN_FILT_SET   0x40001208
#define ROM_PHY_RX11BLR_CFG     0x4000120C
#define ROM_SET_CCA             0x40001210
#define ROM_SET_RX_SENSE        0x40001214
#define ROM_RX_GAIN_FORCE       0x40001218
#define ROM_RFPLL_SET_FREQ      0x4000121C
#define ROM_MHZ2IEEE            0x40001220
#define ROM_CHAN_TO_FREQ        0x40001224
#define ROM_RESTART_CAL         0x40001228
#define ROM_WRITE_RFPLL_SDM     0x4000122C
#define ROM_WAIT_RFPLL_CAL_END  0x40001230
#define ROM_SET_RF_FREQ_OFFSET  0x40001234
#define ROM_SET_RFPLL_FREQ      0x40001238
#define ROM_SET_CHANNEL_RFPLL   0x4000123C
#define ROM_RFPLL_CAP_CORRECT   0x40001240
#define ROM_RFPLL_CAP_INIT_CAL  0x40001244
#define ROM_CHIP_V7_SET_CHAN_ANA 0x40001250
#define ROM_FREQ_SET_REG        0x40001254
#define ROM_GEN_RX_GAIN_TABLE   0x40001258
#define ROM_BT_TXDC_CAL         0x4000125C
#define ROM_BT_TXIQ_CAL         0x40001260
#define ROM_TXIQ_CAL_INIT       0x40001264
#define ROM_TXDC_CAL_INIT       0x40001268
#define ROM_TXDC_CAL            0x4000126C
#define ROM_RFCAL_TXIQ          0x40001278
#define ROM_GET_POWER_ATTEN     0x4000127C
#define ROM_PWDET_REF_CODE      0x40001280
#define ROM_PWDET_CODE_CAL      0x40001284
#define ROM_RFCAL_TXCAP         0x40001288
#define ROM_TX_CAP_INIT         0x4000128C
#define ROM_RFCAL_PWRCTRL       0x40001290
#define ROM_TX_PWCTRL_INIT_CAL  0x40001294
#define ROM_TX_PWCTRL_INIT      0x40001298
#define ROM_BT_TX_PWCTRL_INIT   0x4000129C
#define ROM_SET_TXCAP_REG       0x400012D0
#define ROM_PBUS_FORCE_MODE     0x400012F0
#define ROM_PBUS_XPD_RX_OFF     0x40001308
#define ROM_PBUS_XPD_RX_ON      0x4000130C
#define ROM_PBUS_XPD_TX_OFF     0x40001310
#define ROM_PBUS_XPD_TX_ON      0x40001314
#define ROM_DISABLE_AGC         0x40001338
#define ROM_ENABLE_AGC          0x4000133C
#define ROM_PHY_DISABLE_CCA     0x40001340
#define ROM_PHY_ENABLE_CCA      0x40001344
#define ROM_WRITE_GAIN_MEM      0x40001348
#define ROM_BB_BSS_CBW40_DIG    0x4000134C
#define ROM_CBW2040_CFG         0x40001350
#define ROM_MAC_TX_CHAN_OFFSET  0x40001354
#define ROM_TX_PAON_SET         0x40001358
#define ROM_PWDET_REG_INIT      0x4000135C
#define ROM_I2CMST_REG_INIT     0x40001360
#define ROM_BT_GAIN_OFFSET      0x40001364
#define ROM_FE_REG_INIT         0x40001368
#define ROM_MAC_ENABLE_BB       0x4000136C
#define ROM_BB_WDG_CFG          0x40001370
#define ROM_FE_TXRX_RESET       0x40001374
#define ROM_SET_RX_COMP         0x40001378
#define ROM_AGC_REG_INIT        0x4000137C
#define ROM_BB_REG_INIT         0x40001380
#define ROM_OPEN_I2C_XPD        0x40001384
#define ROM_TXIQ_SET_REG        0x40001388
#define ROM_RXIQ_SET_REG        0x4000138C
#define ROM_SET_TXCLK_EN        0x40001390
#define ROM_SET_RXCLK_EN        0x40001394
#define ROM_BB_WDG_TEST_EN      0x40001398
#define ROM_NOISE_FLOOR_AUTO    0x4000139C
#define ROM_READ_HW_NOISEFLOOR  0x400013A0
#define ROM_IQ_CORR_ENABLE      0x400013A4
#define ROM_WIFI_AGC_SAT_GAIN   0x400013A8
#define ROM_PHY_BBPLL_CAL       0x400013AC
#define ROM_PHY_ANT_INIT        0x400013B0
#define ROM_PHY_SET_BBFREQ_INIT 0x400013B4
#define ROM_WIFI_FBW_SEL        0x400013B8
#define ROM_BT_FILTER_REG       0x400013BC
#define ROM_PHY_RX_SENSE_SET    0x400013C0
#define ROM_TX_STATE_SET        0x400013C4
#define ROM_PHY_CLOSE_PA        0x400013C8
#define ROM_PHY_FREQ_CORRECT    0x400013CC
#define ROM_SET_PBUS_REG        0x400013D0
#define ROM_WIFI_RIFS_MODE_EN   0x400013D4
#define ROM_NRX_FREQ_SET        0x400013D8
#define ROM_FE_ADC_ON           0x400013DC
#define ROM_IQ_EST_ENABLE       0x400013E4
#define ROM_IQ_EST_DISABLE      0x400013E8

// ---- net80211 ROM functions (esp32c6.rom.net80211.ld) ----

#define ROM_ESP_NET80211_VER_GET 0x40000B4C
#define ROM_AMPDU_DISPATCH      0x40000B50
#define ROM_IC_EBUF_RECYCLE_RX  0x40000B70
#define ROM_IC_EBUF_RECYCLE_TX  0x40000B74
#define ROM_IC_RESET_RX_BA      0x40000B78
#define ROM_IEEE80211_ALIGN_EB  0x40000B7C
#define ROM_IEEE80211_AMPDU_AGE 0x40000B84
#define ROM_IEEE80211_TX_ALLOWED 0x40000B8C
#define ROM_IEEE80211_OUTPUT_PENDING 0x40000B90
#define ROM_WIFI_GET_MACADDR    0x40000BA0   // void wifi_get_macaddr(uint8_t mac[6])
#define ROM_WIFI_RF_PHY_DISABLE 0x40000BA4
#define ROM_WIFI_RF_PHY_ENABLE  0x40000BA8
#define ROM_IC_EBUF_ALLOC       0x40000BAC
#define ROM_IEEE80211_COPY_EB_HDR 0x40000BB4
#define ROM_IEEE80211_RECYCLE_CACHE 0x40000BB8
#define ROM_IEEE80211_SEARCH_NODE 0x40000BBC
#define ROM_IEEE80211_CRYPTO_ENCAP 0x40000BC0
#define ROM_IEEE80211_DECAP     0x40000BC8
#define ROM_WIFI_IS_STARTED     0x40000BCC
#define ROM_IEEE80211_GETTID    0x40000BD0

// ---- PP (Protocol Processing) ROM functions (esp32c6.rom.pp.ld) ----

#define ROM_ESP_PP_ROM_VER_GET  0x40000BD8
#define ROM_HAL_MAC_IS_LOW_RATE 0x40000BF8
#define ROM_HAL_MAC_TX_GET_BLKACK 0x40000BFC
#define ROM_IC_GET_TRC          0x40000C04
#define ROM_IC_INTERFACE_ENABLED 0x40000C10
#define ROM_IS_LMAC_IDLE        0x40000C14
#define ROM_LMAC_DISCARD_AGED   0x40000C1C
#define ROM_LMAC_IS_IDLE        0x40000C28
#define ROM_LMAC_IS_LONG_FRAME  0x40000C2C
#define ROM_LMAC_POST_TX_COMPLETE 0x40000C34
#define ROM_LMAC_PROCESS_ALL_TX_TO 0x40000C38
#define ROM_LMAC_PROCESS_COLLISIONS 0x40000C3C
#define ROM_LMAC_REACH_LONG_LIMIT 0x40000C44
#define ROM_LMAC_REACH_SHORT_LIMIT 0x40000C48
#define ROM_LMAC_RECYCLE_MPDU   0x40000C4C
#define ROM_LMAC_RX_DONE        0x40000C50
#define ROM_MAC_TX_SET_DURATION 0x40000C60
#define ROM_MAC_TX_SET_PLCP2    0x40000C6C
#define ROM_PM_DISABLE_SLP_DELAY 0x40000C78
#define ROM_PM_MAC_WAKEUP       0x40000C80
#define ROM_PM_MAC_SLEEP        0x40000C84
#define ROM_PM_ENABLE_SLP_DELAY 0x40000C8C
#define ROM_PM_LOCAL_TSF_PROCESS 0x40000C90
#define ROM_PM_IS_WAKED         0x40000C9C
#define ROM_PM_ON_DATA_RX       0x40000CA8
#define ROM_PM_SLEEP_FOR        0x40000CC4
#define ROM_PP_DEQUEUE_RXQ      0x40000CE4
#define ROM_PP_DEQUEUE_TXQ      0x40000CE8
#define ROM_PP_EMPTY_DELIM_LEN  0x40000CEC
#define ROM_PP_ENQUEUE_RXQ      0x40000CF0
#define ROM_PP_ENQUEUE_TX_DONE  0x40000CF4
#define ROM_PP_GET_TXFRAME      0x40000CF8
#define ROM_PP_PROCESS_RX_PKT_HDR 0x40000D04
#define ROM_PP_RECORD_BAR_RRC   0x40000D0C
#define ROM_PP_RECYCLE_AMPDU    0x40000D10
#define ROM_PP_RECYCLE_RX_PKT   0x40000D14
#define ROM_PP_RESUME_TX_AMPDU  0x40000D1C
#define ROM_PP_SEARCH_TX_QUEUE  0x40000D2C
#define ROM_PP_SEARCH_TXFRAME   0x40000D30
#define ROM_PP_SELECT_NEXT_QUEUE 0x40000D34
#define ROM_PP_SUB_FROM_AMPDU   0x40000D38
#define ROM_PP_TX_PROTO_PROC    0x40000D44
#define ROM_PP_TXQ_UPDATE_BITMAP 0x40000D48
#define ROM_PP_HDRSIZE          0x40000D50
#define ROM_PP_POST             0x40000D54
#define ROM_RC_GET_AMPDU_SCHED  0x40000D5C
#define ROM_RC_UPDATE_RX_DONE   0x40000D60
#define ROM_RC_AMPDU_LOWER_RATE 0x40000D6C
#define ROM_RC_AMPUD_UPRATE     0x40000D70
#define ROM_RC_CLEAR_CUR_AMPDU  0x40000D74
#define ROM_RC_CLEAR_CUR_SCHED  0x40000D78
#define ROM_RC_CLEAR_CUR_STAT   0x40000D7C
#define ROM_RC_LOWER_SCHED      0x40000D84
#define ROM_RC_SET_TX_AMPDU_LIMIT 0x40000D88
#define ROM_RC_TX_UPDATE_PER    0x40000D8C
#define ROM_RC_UPDATE_ACK_SNR   0x40000D90
#define ROM_RC_UP_SCHED         0x40000DA0
#define ROM_RSSI_MARGIN         0x40000DA4
#define ROM_WDEV_DISCARD_FRAME  0x40000DD8
#define ROM_WDEV_GET_NOISE_FLOOR 0x40000DDC
#define ROM_WDEV_INDICATE_AMPDU 0x40000DE0
#define ROM_WDEV_MAC_REG_LOAD   0x40000DE8   // wdev_mac_reg_load
#define ROM_WDEV_MAC_REG_STORE  0x40000DEC   // wdev_mac_reg_store
#define ROM_WDEV_MAC_SPECIAL_LOAD 0x40000DF0
#define ROM_WDEV_MAC_SPECIAL_STORE 0x40000DF4
#define ROM_WDEV_MAC_WAKEUP     0x40000DF8   // wdev_mac_wakeup
#define ROM_WDEV_MAC_SLEEP      0x40000DFC   // wdev_mac_sleep
#define ROM_HAL_MAC_IS_DMA_EN   0x40000E00
#define ROM_HAL_GET_TSF_TIMER   0x40000E4C

// ---- ROM data pointers ----
// phy_param_rom at 0x4087fce8 — PHY parameters
// net80211_funcs at 0x4087ffac — net80211 function table
// pp_wdev_funcs at 0x4087ff70 — PP/WDEV function table
// g_scan at 0x4087ffa8 — scan state
// g_chm at 0x4087ffa4 — channel manager
// g_ic_ptr at 0x4087ffa0 — IC pointer
// if_ctrl_ptr at 0x4087ff50 — interface control
// wifi_get_macaddr at 0x40000ba0 — read efuse mac

// === ROM function call helpers ===

typedef void (*rom_void_fn_t)(void);
typedef void (*rom_void_fn_i_t)(int);
typedef int  (*rom_int_fn_t)(void);
typedef int  (*rom_int_fn_i_t)(int);
typedef void (*rom_void_fn_p_t)(void*);
typedef void (*rom_void_fn_pi_t)(void*, int);

static inline void rom_call_void(uint32_t addr) {
    ((rom_void_fn_t)addr)();
}
static inline void rom_call_void_i(uint32_t addr, int arg) {
    ((rom_void_fn_i_t)addr)(arg);
}
static inline int rom_call_int(uint32_t addr) {
    return ((rom_int_fn_t)addr)();
}
static inline int rom_call_int_i(uint32_t addr, int arg) {
    return ((rom_int_fn_i_t)addr)(arg);
}
static inline void rom_call_void_p(uint32_t addr, void* arg) {
    ((rom_void_fn_p_t)addr)(arg);
}
static inline void rom_call_void_pi(uint32_t addr, void* arg, int arg2) {
    ((rom_void_fn_pi_t)addr)(arg, arg2);
}

#ifdef __cplusplus
}
#endif
#endif