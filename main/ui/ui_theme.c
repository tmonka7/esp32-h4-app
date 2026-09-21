/*
 * Shared styling and small widget factories.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <stdio.h>

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, UI_COL_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_text_color(card, UI_COL_TEXT, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_card_titled(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *title)
{
    lv_obj_t *card = ui_card(parent, w, h);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);

    lv_obj_t *lbl = ui_label(card, title, &lv_font_montserrat_18, UI_COL_TEXT_DIM);
    lv_obj_set_width(lbl, LV_PCT(100));
    return card;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

lv_obj_t *ui_kv_row(lv_obj_t *parent, const char *symbol, const char *key, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 38);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    if (symbol != NULL) {
        ui_label(row, symbol, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    }
    ui_label(row, key, &lv_font_montserrat_16, UI_COL_TEXT_DIM);

    lv_obj_t *val = ui_label(row, value, &lv_font_montserrat_16, UI_COL_TEXT);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    return val;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_color_t color,
                    lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 48);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(color, 40), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, UI_COL_TEXT, 0);
    lv_obj_center(lbl);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_notice(lv_obj_t *parent, const char *symbol, const char *headline,
                    const char *detail)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, UI_COL_CARD_ALT, 0);
    lv_obj_set_style_border_color(box, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);

    if (symbol != NULL) {
        ui_label(box, symbol, &lv_font_montserrat_28, UI_COL_AMBER);
    }
    ui_label(box, headline, &lv_font_montserrat_20, UI_COL_TEXT);

    if (detail != NULL) {
        lv_obj_t *d = ui_label(box, detail, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
        lv_obj_set_width(d, LV_PCT(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    }
    return box;
}

void ui_format_bytes(uint64_t bytes, char *out, size_t out_len)
{
    if (bytes >= (1ULL << 30)) {
        snprintf(out, out_len, "%.1f GB", (double)bytes / (double)(1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(out, out_len, "%.1f MB", (double)bytes / (double)(1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(out, out_len, "%.1f kB", (double)bytes / (double)(1ULL << 10));
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}
