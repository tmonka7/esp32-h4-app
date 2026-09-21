/*
 * Timer and UART screens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bsp/bsp_uart_test.h"
#include "bsp/jc8012p4a1.h"
#include "esp_timer.h"

/* ================================================================== */
/* Timer                                                              */
/* ================================================================== */

static lv_obj_t *s_tmr_arc;
static lv_obj_t *s_tmr_label;
static lv_obj_t *s_tmr_start_lbl;
static lv_obj_t *s_tmr_preset[6];

static uint32_t s_tmr_total_s = 30 * 60;
static uint32_t s_tmr_left_s  = 30 * 60;
static int64_t  s_tmr_deadline_us;
static bool     s_tmr_running;
static bool     s_tmr_fired;

static const uint32_t k_presets[6] = { 60, 300, 600, 1800, 3600, 0 };
static const char *const k_preset_names[6] = {
    "1 min", "5 min", "10 min", "30 min", "1 hour", "Custom"
};

static void timer_render(void)
{
    if (s_tmr_label == NULL) {
        return;
    }

    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                (unsigned)(s_tmr_left_s / 3600),
                (unsigned)((s_tmr_left_s / 60) % 60),
                (unsigned)(s_tmr_left_s % 60));
    lv_label_set_text(s_tmr_label, buf);

    const int32_t pct = s_tmr_total_s ? (int32_t)((s_tmr_left_s * 100) / s_tmr_total_s) : 0;
    lv_arc_set_value(s_tmr_arc, pct);

    if (s_tmr_start_lbl != NULL) {
        lv_label_set_text(s_tmr_start_lbl, s_tmr_running ? "Pause" : "Start");
    }
}

static void preset_cb(lv_event_t *e)
{
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (k_presets[idx] == 0) {
        /* "Custom" steps in five-minute increments, wrapping at two hours.
         * A keypad would need a modal; this keeps the screen self-contained. */
        s_tmr_total_s += 5 * 60;
        if (s_tmr_total_s > 2 * 3600) {
            s_tmr_total_s = 5 * 60;
        }
    } else {
        s_tmr_total_s = k_presets[idx];
    }

    s_tmr_left_s  = s_tmr_total_s;
    s_tmr_running = false;
    s_tmr_fired   = false;

    for (int i = 0; i < 6; i++) {
        const bool active = (i == idx);
        lv_obj_set_style_bg_color(s_tmr_preset[i], active ? UI_COL_ACCENT : UI_COL_CARD_ALT, 0);
    }
    timer_render();
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    if (s_tmr_running) {
        s_tmr_running = false;
    } else {
        if (s_tmr_left_s == 0) {
            s_tmr_left_s = s_tmr_total_s;
        }
        s_tmr_deadline_us = esp_timer_get_time() + (int64_t)s_tmr_left_s * 1000000;
        s_tmr_running = true;
        s_tmr_fired = false;
    }
    timer_render();
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    s_tmr_running = false;
    s_tmr_fired = false;
    s_tmr_left_s = s_tmr_total_s;
    timer_render();
}

void ui_build_timer(lv_obj_t *c)
{
    s_tmr_arc = NULL;
    s_tmr_label = NULL;
    s_tmr_start_lbl = NULL;

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    /* ---- dial ---- */
    lv_obj_t *left = ui_card(c, 460, UI_CONTENT_H - 2 * UI_GUTTER);

    s_tmr_arc = lv_arc_create(left);
    lv_obj_set_size(s_tmr_arc, 300, 300);
    lv_obj_align(s_tmr_arc, LV_ALIGN_TOP_MID, 0, 20);
    lv_arc_set_rotation(s_tmr_arc, 270);
    lv_arc_set_bg_angles(s_tmr_arc, 0, 360);
    lv_arc_set_range(s_tmr_arc, 0, 100);
    lv_obj_remove_style(s_tmr_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_tmr_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_tmr_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_tmr_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_tmr_arc, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_tmr_arc, UI_COL_ACCENT, LV_PART_INDICATOR);

    s_tmr_label = ui_label(left, "00:30:00", &lv_font_montserrat_48, UI_COL_TEXT);
    lv_obj_align(s_tmr_label, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_t *cap = ui_label(left, "Timer", &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 200);

    lv_obj_t *start = ui_button(left, "Start", UI_COL_ACCENT, start_cb, NULL);
    lv_obj_set_size(start, 190, 52);
    lv_obj_align(start, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    s_tmr_start_lbl = lv_obj_get_child(start, 0);

    lv_obj_t *reset = ui_button(left, "Reset", UI_COL_CARD_ALT, reset_cb, NULL);
    lv_obj_set_size(reset, 190, 52);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    /* ---- presets ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER, "Presets");
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *grid = lv_obj_create(right);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, 200);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 6; i++) {
        const bool active = (k_presets[i] == s_tmr_total_s);
        s_tmr_preset[i] = ui_button(grid, k_preset_names[i],
                                    active ? UI_COL_ACCENT : UI_COL_CARD_ALT,
                                    preset_cb, (void *)(intptr_t)i);
        lv_obj_set_size(s_tmr_preset[i], 180, 56);
    }

    lv_obj_t *note = ui_label(right,
        "When the countdown reaches zero the panel beeps through the ES8311, "
        "if the codec is present. Custom adds five minutes per tap.",
        &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

    timer_render();
}

void ui_tick_timer(void)
{
    if (s_tmr_label == NULL) {
        return;
    }

    if (s_tmr_running) {
        const int64_t remain_us = s_tmr_deadline_us - esp_timer_get_time();
        if (remain_us <= 0) {
            s_tmr_left_s = 0;
            s_tmr_running = false;
            if (!s_tmr_fired) {
                s_tmr_fired = true;
                if (bsp_audio_is_ready()) {
                    bsp_audio_play_tone(2000, 250);
                }
            }
        } else {
            /* Round up so the display shows 00:30:00 rather than 00:29:59 the
             * instant the timer starts. */
            s_tmr_left_s = (uint32_t)((remain_us + 999999) / 1000000);
        }
    }
    timer_render();
}

/* ================================================================== */
/* UART                                                               */
/* ================================================================== */

static lv_obj_t *s_u_result;
static lv_obj_t *s_u_detail;
static lv_obj_t *s_u_tx;
static lv_obj_t *s_u_rx;
static lv_obj_t *s_u_err;
static lv_obj_t *s_u_echo_lbl;
static lv_obj_t *s_u_baud_lbl;

static const uint32_t k_bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 921600 };
#define BAUD_COUNT (sizeof(k_bauds) / sizeof(k_bauds[0]))

static void uart_show_result(void)
{
    if (s_u_result == NULL) {
        return;
    }

    bsp_uart_test_result_t r;
    bsp_uart_test_get_last(&r);

    if (!r.valid) {
        lv_label_set_text(s_u_result, "not run");
        lv_obj_set_style_text_color(s_u_result, UI_COL_TEXT_DIM, 0);
        lv_label_set_text(s_u_detail, "Pick a test above.");
        return;
    }

    if (r.pass) {
        lv_label_set_text(s_u_result, LV_SYMBOL_OK "  PASS");
        lv_obj_set_style_text_color(s_u_result, UI_COL_GREEN, 0);
    } else {
        lv_label_set_text(s_u_result, LV_SYMBOL_CLOSE "  FAIL");
        lv_obj_set_style_text_color(s_u_result, UI_COL_RED, 0);
    }

    char buf[192];
    snprintf(buf, sizeof(buf), "%s loopback on UART%d\n%zu sent, %zu received, "
             "%zu matched\n%s",
             r.mode == BSP_UART_TEST_INTERNAL ? "Internal" : "External",
             r.port, r.bytes_sent, r.bytes_received, r.bytes_matched, r.detail);
    lv_label_set_text(s_u_detail, buf);
}

static void test_internal_cb(lv_event_t *e)
{
    (void)e;
    bsp_uart_test_result_t r;
    bsp_uart_test_run(BSP_UART_TEST_INTERNAL, &r);
    bsp_uart_test_log(&r);
    uart_show_result();
}

static void test_external_cb(lv_event_t *e)
{
    (void)e;
    bsp_uart_test_result_t r;
    bsp_uart_test_run(BSP_UART_TEST_EXTERNAL, &r);
    bsp_uart_test_log(&r);
    uart_show_result();
}

static void echo_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_uart_echo_is_running()) {
        bsp_uart_echo_stop();
    } else {
        bsp_uart_echo_start();
    }
    if (s_u_echo_lbl != NULL) {
        lv_label_set_text(s_u_echo_lbl,
                          bsp_uart_echo_is_running() ? "Stop echo" : "Start echo");
    }
}

static void send_cb(lv_event_t *e)
{
    (void)e;
    bsp_uart_test_send("JC8012P4A1 UART OK\r\n");
}

static void baud_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    const uint32_t idx = lv_dropdown_get_selected(dd);
    if (idx < BAUD_COUNT) {
        bsp_uart_test_set_baud(k_bauds[idx]);
        if (s_u_baud_lbl != NULL) {
            char buf[32];
            lv_snprintf(buf, sizeof(buf), "%" PRIu32 " 8N1", k_bauds[idx]);
            lv_label_set_text(s_u_baud_lbl, buf);
        }
    }
}

void ui_build_uart(lv_obj_t *c)
{
    s_u_result = s_u_detail = s_u_tx = s_u_rx = s_u_err = NULL;
    s_u_echo_lbl = s_u_baud_lbl = NULL;

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    /* ---- port and result ---- */
    lv_obj_t *left = ui_card_titled(c, 520, UI_CONTENT_H - 2 * UI_GUTTER, "Test port");

    char buf[64];
    lv_snprintf(buf, sizeof(buf), "UART%d", CONFIG_BSP_UART_TEST_PORT);
    ui_kv_row(left, LV_SYMBOL_USB, "Port", buf);

    lv_snprintf(buf, sizeof(buf), "GPIO%d", CONFIG_BSP_UART_TEST_TX_GPIO);
    ui_kv_row(left, LV_SYMBOL_UPLOAD, "TX", buf);

    lv_snprintf(buf, sizeof(buf), "GPIO%d", CONFIG_BSP_UART_TEST_RX_GPIO);
    ui_kv_row(left, LV_SYMBOL_DOWNLOAD, "RX", buf);

    lv_snprintf(buf, sizeof(buf), "%" PRIu32 " 8N1", bsp_uart_test_get_baud());
    s_u_baud_lbl = ui_kv_row(left, LV_SYMBOL_SETTINGS, "Line", buf);

    s_u_tx  = ui_kv_row(left, LV_SYMBOL_UPLOAD,   "Bytes sent",     "0");
    s_u_rx  = ui_kv_row(left, LV_SYMBOL_DOWNLOAD, "Bytes received", "0");
    s_u_err = ui_kv_row(left, LV_SYMBOL_WARNING,  "Errors",         "0");

    lv_obj_t *res_box = lv_obj_create(left);
    lv_obj_set_width(res_box, LV_PCT(100));
    lv_obj_set_flex_grow(res_box, 1);
    lv_obj_set_style_bg_color(res_box, UI_COL_CARD_ALT, 0);
    lv_obj_set_style_border_width(res_box, 0, 0);
    lv_obj_set_style_radius(res_box, 12, 0);
    lv_obj_set_style_pad_all(res_box, 14, 0);
    lv_obj_set_flex_flow(res_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(res_box, 8, 0);

    s_u_result = ui_label(res_box, "not run", &lv_font_montserrat_28, UI_COL_TEXT_DIM);
    s_u_detail = ui_label(res_box, "Pick a test on the right.",
                          &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_set_width(s_u_detail, LV_PCT(100));
    lv_label_set_long_mode(s_u_detail, LV_LABEL_LONG_WRAP);

    /* ---- actions ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER,
                                     "TX/RX verification");
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *dd = lv_dropdown_create(right);
    lv_obj_set_width(dd, LV_PCT(100));
    lv_dropdown_set_options(dd, "9600\n19200\n38400\n57600\n115200\n230400\n921600");
    for (size_t i = 0; i < BAUD_COUNT; i++) {
        if (k_bauds[i] == bsp_uart_test_get_baud()) {
            lv_dropdown_set_selected(dd, (uint32_t)i);
            break;
        }
    }
    lv_obj_set_style_bg_color(dd, UI_COL_CARD_ALT, 0);
    lv_obj_set_style_border_color(dd, UI_COL_BORDER, 0);
    lv_obj_set_style_text_color(dd, UI_COL_TEXT, 0);
    lv_obj_add_event_cb(dd, baud_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_button(right, LV_SYMBOL_LOOP "  Internal loopback (no wiring)",
              UI_COL_ACCENT, test_internal_cb, NULL);
    ui_button(right, LV_SYMBOL_SHUFFLE "  External loopback (needs a jumper)",
              UI_COL_GREEN, test_external_cb, NULL);

    lv_obj_t *echo = ui_button(right,
                               bsp_uart_echo_is_running() ? "Stop echo" : "Start echo",
                               UI_COL_PURPLE, echo_cb, NULL);
    s_u_echo_lbl = lv_obj_get_child(echo, 0);

    ui_button(right, LV_SYMBOL_UPLOAD "  Send test string", UI_COL_CARD_ALT, send_cb, NULL);

    lv_obj_t *help = ui_label(right,
        "Internal uses the UART peripheral loopback, so it passes without any "
        "wiring and proves the port, clock and baud divisor are right.\n\n"
        "External is the real check: link the TX and RX pins on the FPC3 "
        "expansion header with a jumper, then run it. A pass means the pads "
        "and the board wiring are good.\n\n"
        "UART0 (GPIO37 TX / GPIO38 RX, header CN2 and the CH340) is the "
        "console and is deliberately left alone - if you are reading boot logs "
        "then UART0 TX already works.",
        &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);

    uart_show_result();
}

void ui_tick_uart(void)
{
    if (s_u_tx == NULL) {
        return;
    }

    uint32_t tx = 0;
    uint32_t rx = 0;
    uint32_t errs = 0;
    bsp_uart_test_stats(&tx, &rx, &errs);

    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%" PRIu32, tx);
    lv_label_set_text(s_u_tx, buf);
    lv_snprintf(buf, sizeof(buf), "%" PRIu32, rx);
    lv_label_set_text(s_u_rx, buf);
    lv_snprintf(buf, sizeof(buf), "%" PRIu32, errs);
    lv_label_set_text(s_u_err, buf);
    lv_obj_set_style_text_color(s_u_err, errs ? UI_COL_RED : UI_COL_TEXT, 0);
}
