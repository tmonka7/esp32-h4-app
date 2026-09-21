/*
 * Home screen: clock hero panel plus the app launcher grid.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <time.h>

#include "bsp/jc8012p4a1.h"

static lv_obj_t *s_clock_big;
static lv_obj_t *s_date;

static void tile_cb(lv_event_t *e)
{
    ui_show((ui_screen_id_t)(intptr_t)lv_event_get_user_data(e));
}

/* Driven by the shell tick rather than a timer of its own, so the labels can
 * never outlive or be outlived by their updater. */
void ui_tick_home(void)
{
    if (s_clock_big == NULL || s_date == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char buf[48];
    strftime(buf, sizeof(buf), "%H:%M", &tm_now);
    lv_label_set_text(s_clock_big, buf);

    strftime(buf, sizeof(buf), "%Y-%m-%d  %a", &tm_now);
    lv_label_set_text(s_date, buf);
}

static lv_obj_t *make_tile(lv_obj_t *parent, ui_screen_id_t id)
{
    const ui_screen_desc_t *d = &ui_screen_table()[id];

    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_set_size(tile, 120, 128);
    lv_obj_set_style_bg_color(tile, UI_COL_CARD, 0);
    lv_obj_set_style_bg_color(tile, UI_COL_CARD_ALT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(tile, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 12, 0);
    lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    /* Rounded colour chip behind the glyph, as in the reference design. */
    lv_obj_t *chip = lv_obj_create(tile);
    lv_obj_set_size(chip, 58, 58);
    lv_obj_set_style_bg_color(chip, d->accent, 0);
    lv_obj_set_style_radius(chip, 16, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *glyph = ui_label(chip, d->symbol, &lv_font_montserrat_24, UI_COL_TEXT);
    lv_obj_center(glyph);

    ui_label(tile, d->title, &lv_font_montserrat_14, UI_COL_TEXT);
    return tile;
}

void ui_build_home(lv_obj_t *c)
{
    /* The previous screen was wiped by ui_show(); drop the stale pointers
     * before anything can dereference them. */
    s_clock_big = NULL;
    s_date = NULL;

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ---- left: clock hero ---- */
    lv_obj_t *hero = ui_card(c, 470, UI_CONTENT_H - 2 * UI_GUTTER);
    lv_obj_set_style_bg_color(hero, UI_COL_CARD_ALT, 0);
    lv_obj_set_style_pad_all(hero, 28, 0);

    s_clock_big = ui_label(hero, "--:--", &lv_font_montserrat_48, UI_COL_TEXT);
    lv_obj_align(s_clock_big, LV_ALIGN_TOP_LEFT, 0, 4);

    s_date = ui_label(hero, "----------", &lv_font_montserrat_18, UI_COL_TEXT_DIM);
    lv_obj_align(s_date, LV_ALIGN_TOP_LEFT, 0, 70);

    lv_obj_t *name = ui_label(hero, "JC8012P4A1", &lv_font_montserrat_24, UI_COL_ACCENT);
    lv_obj_align(name, LV_ALIGN_BOTTOM_LEFT, 0, -34);
    lv_obj_t *sub = ui_label(hero, "10.1\" Smart Control Panel  -  ESP32-P4",
                             &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ---- right: launcher grid ---- */
    /* The width has to be concrete, not LV_SIZE_CONTENT: a content-sized
     * ROW_WRAP container has nothing to wrap against and lays every tile out
     * on one row running off the screen. */
    lv_obj_t *grid = lv_obj_create(c);
    lv_obj_set_size(grid, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER);
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 14, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);

    for (int id = UI_SCR_BATTERY; id < UI_SCR_COUNT; id++) {
        make_tile(grid, (ui_screen_id_t)id);
    }
}
