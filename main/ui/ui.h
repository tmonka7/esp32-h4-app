/*
 * Smart Control Panel UI - shared types, palette and widget helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Layout                                                             */
/* ------------------------------------------------------------------ */
#define UI_SCREEN_W       1280
#define UI_SCREEN_H       800
#define UI_STATUSBAR_H    56
#define UI_NAVBAR_H       78
#define UI_CONTENT_H      (UI_SCREEN_H - UI_STATUSBAR_H - UI_NAVBAR_H)
#define UI_GUTTER         16

/* ------------------------------------------------------------------ */
/* Palette - dark navy, matching the reference design                 */
/* ------------------------------------------------------------------ */
/*
 * Each colour is defined once as a 0xRRGGBB literal and exposed in two forms.
 *
 * LV_COLOR_MAKE() expands to a brace-enclosed initialiser list ({b, g, r}),
 * so it is only valid where an initialiser is expected - passing it to a
 * function is a syntax error.  lv_color_hex() is the expression form, but
 * being a call it is not a constant expression and cannot appear in a
 * file-scope initialiser.
 *
 * Use UI_COL_* for arguments and assignments; use UI_COL_INIT(UI_HEX_*) in
 * static/file-scope initialisers such as the screen table in ui_shell.c.
 */
#define UI_COL_INIT(hex) \
    LV_COLOR_MAKE(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)

#define UI_HEX_BG         0x060A12
#define UI_HEX_BAR        0x0B1220
#define UI_HEX_CARD       0x101A2C
#define UI_HEX_CARD_ALT   0x16223A
#define UI_HEX_BORDER     0x1E2D47
#define UI_HEX_TEXT       0xE9EFF8
#define UI_HEX_TEXT_DIM   0x8494AC
#define UI_HEX_ACCENT     0x2F86F6
#define UI_HEX_GREEN      0x2ECC71
#define UI_HEX_AMBER      0xF2B23C
#define UI_HEX_RED        0xE7513F
#define UI_HEX_PURPLE     0x9B6BF2
#define UI_HEX_PINK       0xEC4D7D
#define UI_HEX_CYAN       0x36C5D8

#define UI_COL_BG         lv_color_hex(UI_HEX_BG)
#define UI_COL_BAR        lv_color_hex(UI_HEX_BAR)
#define UI_COL_CARD       lv_color_hex(UI_HEX_CARD)
#define UI_COL_CARD_ALT   lv_color_hex(UI_HEX_CARD_ALT)
#define UI_COL_BORDER     lv_color_hex(UI_HEX_BORDER)
#define UI_COL_TEXT       lv_color_hex(UI_HEX_TEXT)
#define UI_COL_TEXT_DIM   lv_color_hex(UI_HEX_TEXT_DIM)
#define UI_COL_ACCENT     lv_color_hex(UI_HEX_ACCENT)
#define UI_COL_GREEN      lv_color_hex(UI_HEX_GREEN)
#define UI_COL_AMBER      lv_color_hex(UI_HEX_AMBER)
#define UI_COL_RED        lv_color_hex(UI_HEX_RED)
#define UI_COL_PURPLE     lv_color_hex(UI_HEX_PURPLE)
#define UI_COL_PINK       lv_color_hex(UI_HEX_PINK)
#define UI_COL_CYAN       lv_color_hex(UI_HEX_CYAN)

/* ------------------------------------------------------------------ */
/* Screens                                                            */
/* ------------------------------------------------------------------ */
typedef enum {
    UI_SCR_HOME = 0,
    UI_SCR_BATTERY,
    UI_SCR_TIMER,
    UI_SCR_VIDEO,
    UI_SCR_MUSIC,
    UI_SCR_PHOTO,
    UI_SCR_SDCARD,
    UI_SCR_FILES,
    UI_SCR_CAMERA,
    UI_SCR_CAMVIEW,
    UI_SCR_SETTINGS,
    UI_SCR_WIDGETS,
    UI_SCR_ABOUT,
    UI_SCR_UART,
    UI_SCR_COUNT
} ui_screen_id_t;

typedef struct {
    const char *title;
    const char *symbol;     /**< an LV_SYMBOL_* string */
    lv_color_t  accent;
    void (*build)(lv_obj_t *content);
    void (*tick)(void);     /**< called about twice a second while visible */
} ui_screen_desc_t;

/** Build the whole UI and show the home screen. Call with the LVGL lock held. */
void ui_start(void);

/** Switch screens. Safe to call from an LVGL event callback. */
void ui_show(ui_screen_id_t id);

ui_screen_id_t ui_current(void);

/** Descriptor table, indexed by ui_screen_id_t. */
const ui_screen_desc_t *ui_screen_table(void);

/* ------------------------------------------------------------------ */
/* Widget helpers (ui_theme.c)                                        */
/* ------------------------------------------------------------------ */

/** Flat panel with the card background, a 1 px border and rounded corners. */
lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);

/** Card with a heading label along the top. */
lv_obj_t *ui_card_titled(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *title);

lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, lv_color_t color);

/** One "icon  key .......... value" row. Returns the value label so the
 *  caller can keep updating it. */
lv_obj_t *ui_kv_row(lv_obj_t *parent, const char *symbol, const char *key,
                    const char *value);

/** Pill button. Returns the button; the label is its only child. */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t color,
                    lv_event_cb_t cb, void *user_data);

/** Large status banner used when a feature is unavailable (no card, no
 *  codec, no camera). Honest placeholder rather than fake data. */
lv_obj_t *ui_notice(lv_obj_t *parent, const char *symbol, const char *headline,
                    const char *detail);

/** Format a byte count as "12.4 GB" / "512 MB" / "1.2 kB". */
void ui_format_bytes(uint64_t bytes, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
