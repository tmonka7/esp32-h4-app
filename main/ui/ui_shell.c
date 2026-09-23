/*
 * UI shell: status bar, bottom navigation strip and screen switching.
 *
 * One screen object is reused. Switching wipes the content container and calls
 * the new descriptor build(), which keeps peak heap flat regardless of how
 * many screens exist.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_wifi.h"
#include "bsp/jc8012p4a1.h"

/* Per-screen builders, defined in the ui_scr_*.c files. */
void ui_build_home(lv_obj_t *c);
void ui_tick_home(void);
void ui_build_battery(lv_obj_t *c);
void ui_tick_battery(void);
void ui_build_timer(lv_obj_t *c);
void ui_tick_timer(void);
void ui_build_video(lv_obj_t *c);
void ui_build_music(lv_obj_t *c);
void ui_tick_music(void);
void ui_build_photo(lv_obj_t *c);
void ui_build_sdcard(lv_obj_t *c);
void ui_build_files(lv_obj_t *c);
void ui_build_camera(lv_obj_t *c);
void ui_build_camview(lv_obj_t *c);
void ui_tick_camview(void);
void ui_build_settings(lv_obj_t *c);
void ui_build_widgets(lv_obj_t *c);
void ui_tick_widgets(void);
void ui_build_about(lv_obj_t *c);
void ui_build_uart(lv_obj_t *c);
void ui_tick_uart(void);

static const ui_screen_desc_t s_screens[UI_SCR_COUNT] = {
    [UI_SCR_HOME]     = { "Home",         LV_SYMBOL_HOME,         UI_COL_INIT(UI_HEX_ACCENT), ui_build_home,     ui_tick_home },
    [UI_SCR_BATTERY]  = { "Battery",      LV_SYMBOL_BATTERY_FULL, UI_COL_INIT(UI_HEX_GREEN),  ui_build_battery,  ui_tick_battery },
    [UI_SCR_TIMER]    = { "Timer",        LV_SYMBOL_BELL,         UI_COL_INIT(UI_HEX_CYAN),   ui_build_timer,    ui_tick_timer },
    [UI_SCR_VIDEO]    = { "Video Player", LV_SYMBOL_VIDEO,        UI_COL_INIT(UI_HEX_ACCENT), ui_build_video,    NULL },
    [UI_SCR_MUSIC]    = { "Music",        LV_SYMBOL_AUDIO,        UI_COL_INIT(UI_HEX_PINK),   ui_build_music,    ui_tick_music },
    [UI_SCR_PHOTO]    = { "Photo",        LV_SYMBOL_IMAGE,        UI_COL_INIT(UI_HEX_PURPLE), ui_build_photo,    NULL },
    [UI_SCR_SDCARD]   = { "SD Card",      LV_SYMBOL_SD_CARD,      UI_COL_INIT(UI_HEX_PURPLE), ui_build_sdcard,   NULL },
    [UI_SCR_FILES]    = { "File Manager", LV_SYMBOL_DIRECTORY,    UI_COL_INIT(UI_HEX_AMBER),  ui_build_files,    NULL },
    [UI_SCR_CAMERA]   = { "Camera",       LV_SYMBOL_EYE_OPEN,     UI_COL_INIT(UI_HEX_CYAN),   ui_build_camera,   NULL },
    [UI_SCR_CAMVIEW]  = { "Cam Remote",   LV_SYMBOL_WIFI,         UI_COL_INIT(UI_HEX_CYAN),   ui_build_camview,  ui_tick_camview },
    [UI_SCR_SETTINGS] = { "Settings",     LV_SYMBOL_SETTINGS,     UI_COL_INIT(UI_HEX_ACCENT), ui_build_settings, NULL },
    [UI_SCR_WIDGETS]  = { "Widgets",      LV_SYMBOL_LIST,         UI_COL_INIT(UI_HEX_AMBER),  ui_build_widgets,  ui_tick_widgets },
    [UI_SCR_ABOUT]    = { "About",        LV_SYMBOL_BULLET,       UI_COL_INIT(UI_HEX_ACCENT), ui_build_about,    NULL },
    [UI_SCR_UART]     = { "UART",         LV_SYMBOL_USB,          UI_COL_INIT(UI_HEX_GREEN),  ui_build_uart,     ui_tick_uart },
};

static lv_obj_t      *s_root;
static lv_obj_t      *s_content;
static lv_obj_t      *s_title;
static lv_obj_t      *s_back;
static lv_obj_t      *s_clock;
static lv_obj_t      *s_batt_icon;
static lv_obj_t      *s_batt_text;
static lv_obj_t      *s_wifi_icon;
static lv_obj_t      *s_navbar;
static lv_obj_t      *s_nav_btn[UI_SCR_COUNT];
static ui_screen_id_t s_current = UI_SCR_HOME;

const ui_screen_desc_t *ui_screen_table(void)
{
    return s_screens;
}

ui_screen_id_t ui_current(void)
{
    return s_current;
}

/* ------------------------------------------------------------------ */
/* Status bar                                                         */
/* ------------------------------------------------------------------ */

static const char *battery_symbol(int percent)
{
    if (percent < 0)  return LV_SYMBOL_USB;          /* running off USB */
    if (percent > 87) return LV_SYMBOL_BATTERY_FULL;
    if (percent > 62) return LV_SYMBOL_BATTERY_3;
    if (percent > 37) return LV_SYMBOL_BATTERY_2;
    if (percent > 12) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void statusbar_refresh(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", &tm_now);
    lv_label_set_text(s_clock, buf);

    lv_obj_set_style_text_color(s_wifi_icon,
                                app_wifi_is_connected() ? UI_COL_GREEN : UI_COL_TEXT_DIM, 0);

    int mv = 0;
    if (bsp_power_battery_mv(&mv) == ESP_OK) {
        const int pct = bsp_power_battery_percent(mv);
        lv_label_set_text(s_batt_icon, battery_symbol(pct));
        if (pct >= 0) {
            lv_snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_obj_set_style_text_color(s_batt_icon,
                                        pct <= 15 ? UI_COL_RED : UI_COL_GREEN, 0);
        } else {
            /* No cell on CN1: the divider reads the IP5306 rail, not a
             * battery. Say USB rather than inventing a percentage. */
            lv_snprintf(buf, sizeof(buf), "USB");
            lv_obj_set_style_text_color(s_batt_icon, UI_COL_TEXT_DIM, 0);
        }
        lv_label_set_text(s_batt_text, buf);
    } else {
        lv_label_set_text(s_batt_icon, LV_SYMBOL_WARNING);
        lv_label_set_text(s_batt_text, "--");
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_show(UI_SCR_HOME);
}

static void build_statusbar(void)
{
    lv_obj_t *bar = lv_obj_create(s_root);
    lv_obj_set_size(bar, LV_PCT(100), UI_STATUSBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_BAR, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, UI_GUTTER, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_back = lv_button_create(bar);
    lv_obj_set_size(s_back, 44, 36);
    lv_obj_align(s_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_back, 0, 0);
    lv_obj_add_event_cb(s_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(s_back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl, UI_COL_TEXT, 0);
    lv_obj_center(back_lbl);

    s_title = ui_label(bar, "Home", &lv_font_montserrat_20, UI_COL_TEXT);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 52, 0);

    s_batt_text = ui_label(bar, "--", &lv_font_montserrat_16, UI_COL_TEXT);
    lv_obj_align(s_batt_text, LV_ALIGN_RIGHT_MID, 0, 0);

    s_batt_icon = ui_label(bar, LV_SYMBOL_BATTERY_FULL, &lv_font_montserrat_16, UI_COL_GREEN);
    lv_obj_align_to(s_batt_icon, s_batt_text, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    /* Both radios live on the ESP32-C6. Wi-Fi is driven through ESP-Hosted
     * and turns green only once the station holds an IP address; Bluetooth
     * is not brought up, so it stays dimmed. */
    lv_obj_t *bt = ui_label(bar, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(bt, LV_ALIGN_RIGHT_MID, -110, 0);
    s_wifi_icon = ui_label(bar, LV_SYMBOL_WIFI, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(s_wifi_icon, LV_ALIGN_RIGHT_MID, -145, 0);

    s_clock = ui_label(bar, "--:--", &lv_font_montserrat_20, UI_COL_TEXT);
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, -190, 0);
}

/* ------------------------------------------------------------------ */
/* Navigation bar                                                     */
/* ------------------------------------------------------------------ */

static void nav_cb(lv_event_t *e)
{
    const ui_screen_id_t id = (ui_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    ui_show(id);
}

static void nav_highlight(void)
{
    for (int i = 0; i < UI_SCR_COUNT; i++) {
        if (s_nav_btn[i] == NULL) {
            continue;
        }
        const bool active = (i == (int)s_current);
        lv_obj_set_style_bg_opa(s_nav_btn[i], active ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_nav_btn[i], s_screens[i].accent, 0);

        lv_obj_t *icon = lv_obj_get_child(s_nav_btn[i], 0);
        lv_obj_t *text = lv_obj_get_child(s_nav_btn[i], 1);
        lv_obj_set_style_text_color(icon, active ? s_screens[i].accent : UI_COL_TEXT_DIM, 0);
        lv_obj_set_style_text_color(text, active ? UI_COL_TEXT : UI_COL_TEXT_DIM, 0);
    }
}

static void build_navbar(void)
{
    s_navbar = lv_obj_create(s_root);
    lv_obj_set_size(s_navbar, LV_PCT(100), UI_NAVBAR_H);
    lv_obj_align(s_navbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_navbar, UI_COL_BAR, 0);
    lv_obj_set_style_border_color(s_navbar, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(s_navbar, 1, 0);
    lv_obj_set_style_border_side(s_navbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(s_navbar, 0, 0);
    lv_obj_set_style_pad_all(s_navbar, 4, 0);
    lv_obj_set_style_pad_column(s_navbar, 2, 0);
    lv_obj_set_flex_flow(s_navbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_navbar, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_navbar, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_navbar, LV_SCROLLBAR_MODE_OFF);

    /* Home is reachable from the back arrow and the app grid, so it is left
     * out of the strip to keep the remaining thirteen on one screen width. */
    for (int i = 1; i < UI_SCR_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(s_navbar);
        lv_obj_set_size(btn, 92, 66);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, 4, 0);
        lv_obj_add_event_cb(btn, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui_label(btn, s_screens[i].symbol, &lv_font_montserrat_20, UI_COL_TEXT_DIM);
        ui_label(btn, s_screens[i].title, &lv_font_montserrat_12, UI_COL_TEXT_DIM);

        s_nav_btn[i] = btn;
    }
}

/* ------------------------------------------------------------------ */
/* Screen switching                                                   */
/* ------------------------------------------------------------------ */

void ui_show(ui_screen_id_t id)
{
    if (id < 0 || id >= UI_SCR_COUNT) {
        return;
    }

    s_current = id;
    lv_obj_clean(s_content);
    lv_label_set_text(s_title, s_screens[id].title);

    /* Home is the root of the hierarchy; nothing to go back to. */
    if (id == UI_SCR_HOME) {
        lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 4, 0);
    } else {
        lv_obj_clear_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 52, 0);
    }

    nav_highlight();

    if (s_screens[id].build != NULL) {
        s_screens[id].build(s_content);
    }
    if (s_screens[id].tick != NULL) {
        s_screens[id].tick();
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    statusbar_refresh();
    if (s_screens[s_current].tick != NULL) {
        s_screens[s_current].tick();
    }
}

void ui_start(void)
{
    s_root = lv_screen_active();
    lv_obj_set_style_bg_color(s_root, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    build_statusbar();
    build_navbar();

    s_content = lv_obj_create(s_root);
    lv_obj_set_size(s_content, LV_PCT(100), UI_CONTENT_H);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, UI_STATUSBAR_H);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, UI_GUTTER, 0);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);

    ui_show(UI_SCR_HOME);

    lv_timer_create(tick_cb, 500, NULL);
    statusbar_refresh();
}
