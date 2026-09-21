/*
 * Board support package for the Guition JC8012P4A1
 * (ESP32-P4NRW32 + ESP32-C6-MINI-1U, 10.1" 800x1280 MIPI-DSI panel)
 *
 * Pin assignments below are taken from the board schematics
 * (5-Schematic/3_ESP32-P4.png, 4_CONN.png, 7_CODEC.png in the vendor package)
 * and cross-checked against the vendor ESP-IDF BSP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Pin map
 * ===========================================================================*/

/* --- I2C0: GSL3680 touch (0x40), ES8311 codec (0x18), RX8025T RTC (0x32) --- */
#define BSP_I2C_SDA             (GPIO_NUM_7)
#define BSP_I2C_SCL             (GPIO_NUM_8)
#define BSP_I2C_PORT            (I2C_NUM_0)
#define BSP_I2C_FREQ_HZ         (400000)

/* --- Display: JD9365 800x1280, MIPI-DSI 2 lanes --------------------------- */
#define BSP_LCD_RST             (GPIO_NUM_27)
#define BSP_LCD_BACKLIGHT       (GPIO_NUM_23)   /* LEDC PWM, active high      */
#define BSP_LCD_H_RES           (800)           /* native (portrait)          */
#define BSP_LCD_V_RES           (1280)
#define BSP_LCD_DSI_LANE_NUM    (2)
#define BSP_LCD_DSI_LANE_MBPS   (1500)
#define BSP_LCD_DPI_CLK_MHZ     (60)
#define BSP_LCD_DSI_PHY_LDO_CHAN  (3)           /* on-chip LDO VO3 -> DPHY    */
#define BSP_LCD_DSI_PHY_LDO_MV    (2500)

/* Orientation the UI is laid out in. The panel is physically portrait, the
 * control-panel UI is landscape, so LVGL rotates 90 degrees in software.
 * These follow CONFIG_BSP_DISPLAY_ROTATION_* so they stay truthful if the
 * rotation is changed. */
#if defined(CONFIG_BSP_DISPLAY_ROTATION_90) || defined(CONFIG_BSP_DISPLAY_ROTATION_270)
#define BSP_UI_H_RES            (BSP_LCD_V_RES)
#define BSP_UI_V_RES            (BSP_LCD_H_RES)
#else
#define BSP_UI_H_RES            (BSP_LCD_H_RES)
#define BSP_UI_V_RES            (BSP_LCD_V_RES)
#endif

/* --- Touch: GSL3680 ------------------------------------------------------- */
#define BSP_TOUCH_RST           (GPIO_NUM_22)
#define BSP_TOUCH_INT           (GPIO_NUM_21)

/* --- TF card: SDMMC slot 0 (IOMUX-fixed pins on ESP32-P4) ----------------- */
#define BSP_SD_CLK              (GPIO_NUM_43)
#define BSP_SD_CMD              (GPIO_NUM_44)
#define BSP_SD_D0               (GPIO_NUM_39)
#define BSP_SD_D1               (GPIO_NUM_40)
#define BSP_SD_D2               (GPIO_NUM_41)
#define BSP_SD_D3               (GPIO_NUM_42)
#define BSP_SD_PWR_LDO_CHAN     (4)             /* card VDD = ESP_LDO_VO4     */
#define BSP_SD_MOUNT_POINT      "/sdcard"

/* --- Audio: ES8311 codec + NS4150 amplifier ------------------------------- */
#define BSP_I2S_MCLK            (GPIO_NUM_13)
#define BSP_I2S_BCLK            (GPIO_NUM_12)
#define BSP_I2S_WS              (GPIO_NUM_10)   /* LRCK                       */
#define BSP_I2S_DOUT            (GPIO_NUM_9)    /* P4 -> codec (playback)     */
#define BSP_I2S_DIN             (GPIO_NUM_11)   /* codec -> P4 (capture)      */
#define BSP_POWER_AMP_IO        (GPIO_NUM_20)   /* PA_CTRL, active high       */
#define BSP_ES8311_I2C_ADDR     (0x18)

/* --- RTC: RX8025T --------------------------------------------------------- */
#define BSP_RX8025T_I2C_ADDR    (0x32)

/* --- Battery sense -------------------------------------------------------- */
/* IP5306 power path with a Li-ion connector (CN1). BAT+ is divided by
 * R2 = 68k (top) / R6 = 100k (bottom) and brought to GPIO52 through R4 (0R).
 * GPIO52 is ADC2 channel 3 on the ESP32-P4. */
#define BSP_BAT_ADC_GPIO        (GPIO_NUM_52)
#define BSP_BAT_ADC_UNIT        (ADC_UNIT_2)
#define BSP_BAT_ADC_CHANNEL     (ADC_CHANNEL_3)
#define BSP_BAT_DIV_TOP_KOHM    (68)
#define BSP_BAT_DIV_BOT_KOHM    (100)

/* --- Misc ----------------------------------------------------------------- */
#define BSP_WS2812_DATA         (GPIO_NUM_26)   /* single on-board RGB LED    */

/* --- UART ----------------------------------------------------------------- */
/* UART0 is the console. It leaves the module on GPIO37/38 and is wired both to
 * the on-board CH340 and to the 4-pin MX1.25 header CN2
 * (1 = VIN, 2 = TXD, 3 = RXD, 4 = GND). */
#define BSP_UART0_TX            (GPIO_NUM_37)
#define BSP_UART0_RX            (GPIO_NUM_38)

/* ===========================================================================
 * I2C
 * ===========================================================================*/
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

/** Probe one 7-bit address on the shared bus. */
bool bsp_i2c_probe(uint8_t addr_7bit);

/* ===========================================================================
 * Display + touch + LVGL
 * ===========================================================================*/

/**
 * Bring up DSI PHY power, the JD9365 panel, the GSL3680 touch controller and
 * the LVGL port task. Returns the LVGL display, already rotated to landscape.
 */
lv_display_t *bsp_display_start(void);

/** Take/release the LVGL mutex. Every lv_* call made from outside an LVGL
 *  callback must be wrapped in these. */
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

esp_lcd_touch_handle_t bsp_touch_get_handle(void);

/* Backlight, 0..100 %. bsp_display_start() leaves it at
 * CONFIG_BSP_DISPLAY_BRIGHTNESS_DEFAULT. */
esp_err_t bsp_display_brightness_set(int percent);
int       bsp_display_brightness_get(void);

/* ===========================================================================
 * TF card
 * ===========================================================================*/
esp_err_t     bsp_sdcard_mount(void);
esp_err_t     bsp_sdcard_unmount(void);
bool          bsp_sdcard_is_mounted(void);
sdmmc_card_t *bsp_sdcard_get_card(void);

/** Total/used bytes of the mounted FAT volume. Returns ESP_ERR_INVALID_STATE
 *  when no card is mounted. */
esp_err_t bsp_sdcard_usage(uint64_t *out_total_bytes, uint64_t *out_used_bytes);

/* ===========================================================================
 * Audio (ES8311 + NS4150)
 * ===========================================================================*/
esp_err_t bsp_audio_init(uint32_t sample_rate_hz);
bool      bsp_audio_is_ready(void);
esp_err_t bsp_audio_set_volume(int percent);          /* 0..100 */
int       bsp_audio_get_volume(void);
esp_err_t bsp_audio_amp_enable(bool enable);
esp_err_t bsp_audio_write(const void *pcm, size_t bytes, size_t *bytes_written);

/** Short test tone: proves codec + amplifier + I2S are alive. */
esp_err_t bsp_audio_play_tone(uint32_t freq_hz, uint32_t ms);

/* ===========================================================================
 * Power / battery
 * ===========================================================================*/
esp_err_t bsp_power_init(void);

/** Battery terminal voltage in millivolts, corrected for the divider. */
esp_err_t bsp_power_battery_mv(int *out_mv);

/** Rough state of charge from the resting voltage of a single Li-ion cell.
 *  Returns 0..100, or -1 if the reading is out of range for a cell. */
int bsp_power_battery_percent(int mv);

/** SoC junction temperature in degrees Celsius. */
esp_err_t bsp_power_temperature(float *out_celsius);

/* ===========================================================================
 * RTC (RX8025T)
 * ===========================================================================*/
esp_err_t bsp_rtc_init(void);
bool      bsp_rtc_present(void);
esp_err_t bsp_rtc_get_time(struct tm *out_tm);
esp_err_t bsp_rtc_set_time(const struct tm *in_tm);
/** Read the RTC and push it into the system clock (settimeofday). */
esp_err_t bsp_rtc_sync_to_system(void);

/* ===========================================================================
 * Board
 * ===========================================================================*/
/** I2C + RTC + backlight + display + touch + LVGL. The TF card and the audio
 *  codec are brought up separately so a missing card or codec cannot block
 *  the UI from starting. */
esp_err_t bsp_board_init(void);

#ifdef __cplusplus
}
#endif
