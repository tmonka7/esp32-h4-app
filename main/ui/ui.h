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
 * Use LVGL's own color constructor here; a compound literal like
 * ((lv_color_t){ .full = ... }) is not a constant expression and cannot be
 * used in file-scope/static initializers in plain C.
 */
#define UI_COL_RGB(r, g, b) LV_COLOR_MAKE((r), (g), (b))

#define UI_COL_BG         UI_COL_RGB(0x06, 0x0A, 0x12)
#define UI_COL_BAR        UI_COL_RGB(0x0B, 0x12, 0x20)
#define UI_COL_CARD       UI_COL_RGB(0x10, 0x1A, 0x2C)
#define UI_COL_CARD_ALT   UI_COL_RGB(0x16, 0x22, 0x3A)
#define UI_COL_BORDER     UI_COL_RGB(0x1E, 0x2D, 0x47)
#define UI_COL_TEXT       UI_COL_RGB(0xE9, 0xEF, 0xF8)
#define UI_COL_TEXT_DIM   UI_COL_RGB(0x84, 0x94, 0xAC)
#define UI_COL_ACCENT     UI_COL_RGB(0x2F, 0x86, 0xF6)
#define UI_COL_GREEN      UI_COL_RGB(0x2E, 0xCC, 0x71)
#define UI_COL_AMBER      UI_COL_RGB(0xF2, 0xB2, 0x3C)
#define UI_COL_RED        UI_COL_RGB(0xE7, 0x51, 0x3F)
#define UI_COL_PURPLE     UI_COL_RGB(0x9B, 0x6B, 0xF2)
#define UI_COL_PINK       UI_COL_RGB(0xEC, 0x4D, 0x7D)
#define UI_COL_CYAN       UI_COL_RGB(0x36, 0xC5, 0xD8)

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
