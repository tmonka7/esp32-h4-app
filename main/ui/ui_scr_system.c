/*
 * Battery, Widgets, Settings and About screens.
 *
 * Everything shown here is read from the hardware. Where a value genuinely
 * cannot be measured on this board (charge state, Wi-Fi link) the screen says
 * so instead of showing a plausible-looking number.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/jc8012p4a1.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ================================================================== */
/* Battery                                                            */
/* ================================================================== */

#define BATT_HISTORY_LEN 60     /* 60 samples at 2 Hz = last 30 s */

static lv_obj_t       *s_bat_arc;
static lv_obj_t       *s_bat_pct;
static lv_obj_t       *s_bat_volt;
static lv_obj_t       *s_bat_temp;
static lv_obj_t       *s_bat_state;
static lv_obj_t       *s_bat_chart;
static lv_chart_series_t *s_bat_series;

void ui_build_battery(lv_obj_t *c)
{
    s_bat_arc = s_bat_pct = s_bat_volt = s_bat_temp = s_bat_state = NULL;
    s_bat_chart = NULL;
    s_bat_series = NULL;

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    /* ---- gauge ---- */
    lv_obj_t *left = ui_card(c, 400, UI_CONTENT_H - 2 * UI_GUTTER);

    s_bat_arc = lv_arc_create(left);
    lv_obj_set_size(s_bat_arc, 220, 220);
    lv_obj_align(s_bat_arc, LV_ALIGN_TOP_MID, 0, 10);
    lv_arc_set_rotation(s_bat_arc, 135);
    lv_arc_set_bg_angles(s_bat_arc, 0, 270);
    lv_arc_set_range(s_bat_arc, 0, 100);
    lv_arc_set_value(s_bat_arc, 0);
    lv_obj_remove_style(s_bat_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_bat_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_bat_arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_bat_arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_bat_arc, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_bat_arc, UI_COL_GREEN, LV_PART_INDICATOR);

    s_bat_pct = ui_label(left, "--", &lv_font_montserrat_48, UI_COL_TEXT);
    lv_obj_align(s_bat_pct, LV_ALIGN_TOP_MID, 0, 95);

    s_bat_state = ui_label(left, "reading...", &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(s_bat_state, LV_ALIGN_TOP_MID, 0, 250);

    lv_obj_t *rows = lv_obj_create(left);
    lv_obj_set_size(rows, LV_PCT(100), 160);
    lv_obj_align(rows, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(rows, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rows, 0, 0);
    lv_obj_set_style_pad_all(rows, 0, 0);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(rows, LV_OBJ_FLAG_SCROLLABLE);

    s_bat_volt = ui_kv_row(rows, LV_SYMBOL_CHARGE, "Voltage", "-- V");
    s_bat_temp = ui_kv_row(rows, LV_SYMBOL_WARNING, "SoC temperature", "-- C");
    ui_kv_row(rows, LV_SYMBOL_SETTINGS, "Sense pin", "GPIO52 / ADC2 ch3");
    ui_kv_row(rows, LV_SYMBOL_SETTINGS, "Divider", "68k / 100k");

    /* ---- history ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER,
                                     "Terminal voltage, last 30 seconds");
    lv_obj_set_flex_grow(right, 1);

    s_bat_chart = lv_chart_create(right);
    lv_obj_set_width(s_bat_chart, LV_PCT(100));
    lv_obj_set_flex_grow(s_bat_chart, 1);
    lv_chart_set_type(s_bat_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_bat_chart, BATT_HISTORY_LEN);
    lv_chart_set_range(s_bat_chart, LV_CHART_AXIS_PRIMARY_Y, 3000, 4400);
    lv_chart_set_update_mode(s_bat_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(s_bat_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bat_chart, 0, 0);
    lv_obj_set_style_line_width(s_bat_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_bat_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_bat_chart, UI_COL_BORDER, LV_PART_MAIN);
    s_bat_series = lv_chart_add_series(s_bat_chart, UI_COL_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *note = ui_label(right,
        "The IP5306 on this board has no I2C or status outputs wired to the "
        "ESP32-P4, so charge/discharge state cannot be read back - only the "
        "voltage on the divider.",
        &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

void ui_tick_battery(void)
{
    if (s_bat_pct == NULL) {
        return;
    }

    char buf[48];
    int mv = 0;

    if (bsp_power_battery_mv(&mv) == ESP_OK) {
        const int pct = bsp_power_battery_percent(mv);

        lv_snprintf(buf, sizeof(buf), "%d.%02d V", mv / 1000, (mv % 1000) / 10);
        lv_label_set_text(s_bat_volt, buf);

        if (pct >= 0) {
            lv_arc_set_value(s_bat_arc, pct);
            lv_snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(s_bat_pct, buf);
            lv_label_set_text(s_bat_state, "Li-ion cell on CN1");
            lv_obj_set_style_arc_color(s_bat_arc,
                                       pct <= 15 ? UI_COL_RED : UI_COL_GREEN, LV_PART_INDICATOR);
        } else {
            lv_arc_set_value(s_bat_arc, 0);
            lv_label_set_text(s_bat_pct, LV_SYMBOL_USB);
            lv_label_set_text(s_bat_state, "No cell fitted - running from USB");
        }

        if (s_bat_series != NULL) {
            lv_chart_set_next_value(s_bat_chart, s_bat_series, (int32_t)mv);
        }
    } else {
        lv_label_set_text(s_bat_volt, "unavailable");
        lv_label_set_text(s_bat_state, "ADC not initialised");
    }

    float celsius = 0.0f;
    if (bsp_power_temperature(&celsius) == ESP_OK) {
        lv_snprintf(buf, sizeof(buf), "%d.%d C", (int)celsius,
                    (int)((celsius - (float)(int)celsius) * 10.0f));
        lv_label_set_text(s_bat_temp, buf);
    } else {
        lv_label_set_text(s_bat_temp, "n/a");
    }
}

/* ================================================================== */
/* Widgets                                                            */
/* ================================================================== */

static lv_obj_t *s_w_heap;
static lv_obj_t *s_w_psram;
static lv_obj_t *s_w_uptime;
static lv_obj_t *s_w_temp;
static lv_obj_t *s_w_tasks;
static lv_obj_t *s_w_bright;

static lv_obj_t *widget_tile(lv_obj_t *parent, const char *symbol, const char *title,
                             lv_color_t accent, lv_obj_t **out_value)
{
    lv_obj_t *tile = ui_card(parent, 236, 128);
    lv_obj_set_style_bg_color(tile, UI_COL_CARD, 0);

    lv_obj_t *glyph = ui_label(tile, symbol, &lv_font_montserrat_20, accent);
    lv_obj_align(glyph, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *t = ui_label(tile, title, &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 30, 2);

    *out_value = ui_label(tile, "--", &lv_font_montserrat_28, UI_COL_TEXT);
    lv_obj_align(*out_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return tile;
}

void ui_build_widgets(lv_obj_t *c)
{
    s_w_heap = s_w_psram = s_w_uptime = s_w_temp = s_w_tasks = s_w_bright = NULL;

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(c, UI_GUTTER, 0);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    widget_tile(c, LV_SYMBOL_DRIVE,    "Internal heap free", UI_COL_ACCENT, &s_w_heap);
    widget_tile(c, LV_SYMBOL_SD_CARD,  "PSRAM free",         UI_COL_PURPLE, &s_w_psram);
    widget_tile(c, LV_SYMBOL_REFRESH,  "Uptime",             UI_COL_GREEN,  &s_w_uptime);
    widget_tile(c, LV_SYMBOL_WARNING,  "SoC temperature",    UI_COL_AMBER,  &s_w_temp);
    widget_tile(c, LV_SYMBOL_LIST,     "FreeRTOS tasks",     UI_COL_CYAN,   &s_w_tasks);
    widget_tile(c, LV_SYMBOL_EYE_OPEN, "Backlight",          UI_COL_PINK,   &s_w_bright);
}

void ui_tick_widgets(void)
{
    if (s_w_heap == NULL) {
        return;
    }

    char buf[32];

    ui_format_bytes(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), buf, sizeof(buf));
    lv_label_set_text(s_w_heap, buf);

    ui_format_bytes(heap_caps_get_free_size(MALLOC_CAP_SPIRAM), buf, sizeof(buf));
    lv_label_set_text(s_w_psram, buf);

    const int64_t up_s = esp_timer_get_time() / 1000000;
    lv_snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                (int)(up_s / 3600), (int)((up_s / 60) % 60), (int)(up_s % 60));
    lv_label_set_text(s_w_uptime, buf);

    float celsius = 0.0f;
    if (bsp_power_temperature(&celsius) == ESP_OK) {
        lv_snprintf(buf, sizeof(buf), "%d C", (int)celsius);
    } else {
        lv_snprintf(buf, sizeof(buf), "n/a");
    }
    lv_label_set_text(s_w_temp, buf);

    lv_snprintf(buf, sizeof(buf), "%u", (unsigned)uxTaskGetNumberOfTasks());
    lv_label_set_text(s_w_tasks, buf);

    lv_snprintf(buf, sizeof(buf), "%d%%", bsp_display_brightness_get());
    lv_label_set_text(s_w_bright, buf);
}

/* ================================================================== */
/* Settings                                                           */
/* ================================================================== */

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bsp_display_brightness_set((int)lv_slider_get_value(slider));
}

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bsp_audio_set_volume((int)lv_slider_get_value(slider));
}

static void tone_cb(lv_event_t *e)
{
    (void)e;
    /* 1 kHz for 300 ms: short enough not to block the LVGL task for long,
     * loud enough to prove the codec, the amplifier and PA_CTRL all work. */
    bsp_audio_play_tone(1000, 300);
}

static void remount_cb(lv_event_t *e)
{
    (void)e;
    bsp_sdcard_unmount();
    bsp_sdcard_mount();
    ui_show(UI_SCR_SDCARD);
}

static void restart_cb(lv_event_t *e)
{
    (void)e;
    esp_restart();
}

static lv_obj_t *settings_slider(lv_obj_t *parent, const char *label, int value,
                                 lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 74);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    ui_label(row, label, &lv_font_montserrat_16, UI_COL_TEXT_DIM);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COL_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return slider;
}

void ui_build_settings(lv_obj_t *c)
{
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    /* ---- display + sound ---- */
    lv_obj_t *left = ui_card_titled(c, 480, UI_CONTENT_H - 2 * UI_GUTTER, "Display and sound");

    settings_slider(left, "Backlight brightness", bsp_display_brightness_get(), brightness_cb);

    if (bsp_audio_is_ready()) {
        settings_slider(left, "Speaker volume", bsp_audio_get_volume(), volume_cb);
        ui_button(left, LV_SYMBOL_VOLUME_MAX "  Play 1 kHz test tone", UI_COL_ACCENT,
                  tone_cb, NULL);
    } else {
        ui_notice(left, LV_SYMBOL_MUTE, "Audio codec not responding",
                  "No ES8311 answered at I2C address 0x18. Volume and the test "
                  "tone are unavailable.");
    }

    /* ---- system ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER, "System");
    lv_obj_set_flex_grow(right, 1);

    char buf[64];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    ui_kv_row(right, LV_SYMBOL_BELL, "System clock", buf);

    ui_kv_row(right, LV_SYMBOL_SETTINGS, "Hardware RTC",
              bsp_rtc_present() ? "RX8025T @ 0x32" : "not detected");

    lv_snprintf(buf, sizeof(buf), "%dx%d, rotated %s",
                BSP_UI_H_RES, BSP_UI_V_RES,
#if defined(CONFIG_BSP_DISPLAY_ROTATION_90)
                "90 deg"
#elif defined(CONFIG_BSP_DISPLAY_ROTATION_180)
                "180 deg"
#elif defined(CONFIG_BSP_DISPLAY_ROTATION_270)
                "270 deg"
#else
                "0 deg"
#endif
    );
    ui_kv_row(right, LV_SYMBOL_IMAGE, "Panel", buf);

    ui_kv_row(right, LV_SYMBOL_SD_CARD, "TF card",
              bsp_sdcard_is_mounted() ? "mounted at /sdcard" : "not mounted");

    ui_button(right, LV_SYMBOL_REFRESH "  Remount TF card", UI_COL_ACCENT, remount_cb, NULL);
    ui_button(right, LV_SYMBOL_POWER "  Restart", UI_COL_RED, restart_cb, NULL);
}

/* ================================================================== */
/* About                                                              */
/* ================================================================== */

void ui_build_about(lv_obj_t *c)
{
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    lv_obj_t *left = ui_card(c, 460, UI_CONTENT_H - 2 * UI_GUTTER);
    lv_obj_set_style_bg_color(left, UI_COL_CARD_ALT, 0);

    lv_obj_t *name = ui_label(left, "JC8012P4A1", &lv_font_montserrat_36, UI_COL_TEXT);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *sub = ui_label(left, "BSP for ESP32-P4", &lv_font_montserrat_20, UI_COL_ACCENT);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *tag = ui_label(left, "10.1\" Touch Display  -  Smart Control Panel",
                             &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(tag, LV_ALIGN_TOP_MID, 0, 128);

    lv_obj_t *chip = lv_obj_create(left);
    lv_obj_set_size(chip, 160, 120);
    lv_obj_align(chip, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(chip, UI_COL_CARD, 0);
    lv_obj_set_style_border_color(chip, UI_COL_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_radius(chip, 16, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *chip_lbl = ui_label(chip, "ESP32-P4", &lv_font_montserrat_20, UI_COL_TEXT);
    lv_obj_center(chip_lbl);

    lv_obj_t *motto = ui_label(left, "Better Display  -  Smarter Life",
                               &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(motto, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* ---- facts, all read from the running system ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER,
                                     "Firmware and silicon");
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_scrollbar_mode(right, LV_SCROLLBAR_MODE_AUTO);

    char buf[80];

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    lv_snprintf(buf, sizeof(buf), "%s rev v%d.%d, %d core(s)",
                CONFIG_IDF_TARGET, chip_info.revision / 100, chip_info.revision % 100,
                chip_info.cores);
    ui_kv_row(right, LV_SYMBOL_SETTINGS, "SoC", buf);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ui_format_bytes(flash_size, buf, sizeof(buf));
    } else {
        lv_snprintf(buf, sizeof(buf), "unknown");
    }
    ui_kv_row(right, LV_SYMBOL_SAVE, "Flash", buf);

#if CONFIG_SPIRAM
    ui_format_bytes(esp_psram_get_size(), buf, sizeof(buf));
#else
    lv_snprintf(buf, sizeof(buf), "disabled");
#endif
    ui_kv_row(right, LV_SYMBOL_DRIVE, "PSRAM", buf);

    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        lv_snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        lv_snprintf(buf, sizeof(buf), "unavailable");
    }
    ui_kv_row(right, LV_SYMBOL_GPS, "Device ID (base MAC)", buf);

    ui_kv_row(right, LV_SYMBOL_DOWNLOAD, "ESP-IDF", esp_get_idf_version());

    const esp_app_desc_t *app = esp_app_get_description();
    ui_kv_row(right, LV_SYMBOL_FILE, "Firmware version", app->version);
    lv_snprintf(buf, sizeof(buf), "%s %s", app->date, app->time);
    ui_kv_row(right, LV_SYMBOL_BELL, "Build date", buf);

    lv_snprintf(buf, sizeof(buf), "LVGL %d.%d.%d",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ui_kv_row(right, LV_SYMBOL_IMAGE, "Graphics", buf);

    ui_kv_row(right, LV_SYMBOL_IMAGE, "Panel", "JD9365 800x1280 MIPI-DSI, 2 lanes");
    ui_kv_row(right, LV_SYMBOL_EYE_OPEN, "Touch", "GSL3680 @ 0x40");
    ui_kv_row(right, LV_SYMBOL_AUDIO, "Codec",
              bsp_audio_is_ready() ? "ES8311 + NS4150" : "ES8311 not detected");
    ui_kv_row(right, LV_SYMBOL_USB, "Console UART", "UART0, GPIO37 TX / GPIO38 RX");
}
