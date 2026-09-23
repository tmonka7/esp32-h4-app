/*
 * Cam Remote screen: live view of a network camera's MJPEG stream
 * (GET http://<camera>:81/stream, as served by ESP32-CAM CameraWebServer).
 *
 * The stream runs only while this screen is open: an ESP32-CAM serves one
 * stream client at a time, and decoding costs CPU the other screens can use.
 * Network I/O and JPEG decode live in cam_stream.c; this file only swaps the
 * decoded RGB565 frame into an lv_image.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "cam_stream.h"

#define FRAME_POLL_MS 15

void ui_tick_camview(void);

static lv_obj_t   *s_frame;
static lv_obj_t   *s_img;
static lv_obj_t   *s_placeholder;
static lv_obj_t   *s_url_ta;
static lv_obj_t   *s_kb;
static lv_obj_t   *s_run_lbl;
static lv_obj_t   *s_fit_lbl;
static lv_obj_t   *s_state_val;
static lv_obj_t   *s_res_val;
static lv_obj_t   *s_fps_val;
static lv_obj_t   *s_size_val;
static lv_obj_t   *s_drop_val;
static lv_obj_t   *s_msg;
static lv_timer_t *s_poll_timer;

/* Two descriptors, alternated per frame, so a new frame is always a new
 * image source and LVGL never serves a stale cached header for it. */
static lv_image_dsc_t s_dsc[2];
static int            s_dsc_i;
static bool           s_fit = true;
static bool           s_have_frame;
static uint32_t       s_shown;          /* frames put on screen */
static uint32_t       s_fps_mark_frames;
static uint32_t       s_fps_mark_tick;

/* ------------------------------------------------------------------ */
/* Frame display                                                      */
/* ------------------------------------------------------------------ */

static void apply_scale(void)
{
    const lv_image_dsc_t *d = &s_dsc[s_dsc_i];
    if (!s_have_frame || d->header.w == 0 || d->header.h == 0) {
        return;
    }

    uint32_t scale = LV_SCALE_NONE;
    if (s_fit) {
        const int32_t bw = lv_obj_get_content_width(s_frame);
        const int32_t bh = lv_obj_get_content_height(s_frame);
        const uint32_t sx = (uint32_t)bw * LV_SCALE_NONE / d->header.w;
        const uint32_t sy = (uint32_t)bh * LV_SCALE_NONE / d->header.h;
        scale = sx < sy ? sx : sy;
    }
    if (lv_image_get_scale(s_img) != (int32_t)scale) {
        lv_image_set_scale(s_img, scale);
    }
}

static void poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    const uint16_t *px;
    uint16_t w, h;

    if (s_img == NULL || !cam_stream_take_frame(&px, &w, &h)) {
        return;
    }

    s_dsc_i = !s_dsc_i;
    lv_image_dsc_t *d = &s_dsc[s_dsc_i];
    lv_image_cache_drop(d);
    lv_image_header_cache_drop(d);

    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = w * sizeof(uint16_t);
    d->data_size = (uint32_t)w * h * sizeof(uint16_t);
    d->data = (const uint8_t *)px;

    lv_image_set_src(s_img, d);

    if (!s_have_frame) {
        s_have_frame = true;
        lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    apply_scale();
    s_shown++;
}

/* ------------------------------------------------------------------ */
/* Controls                                                           */
/* ------------------------------------------------------------------ */

static void start_stream(void)
{
    const char *url = lv_textarea_get_text(s_url_ta);
    if (url == NULL || url[0] == '\0') {
        return;
    }
    s_shown = 0;
    s_fps_mark_frames = 0;
    s_fps_mark_tick = lv_tick_get();
    cam_stream_start(url);
}

static void run_cb(lv_event_t *e)
{
    (void)e;
    if (cam_stream_is_running()) {
        cam_stream_stop();
    } else {
        start_stream();
    }
    ui_tick_camview();
}

static void fit_cb(lv_event_t *e)
{
    (void)e;
    s_fit = !s_fit;
    lv_label_set_text(s_fit_lbl, s_fit ? "Fit" : "1:1");
    apply_scale();
}

static void kb_hide(void)
{
    lv_keyboard_set_textarea(s_kb, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void ta_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, s_url_ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
    } else if (code == LV_EVENT_DEFOCUSED) {
        kb_hide();
    }
}

static void kb_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        cam_stream_save_url(lv_textarea_get_text(s_url_ta));
        lv_obj_remove_state(s_url_ta, LV_STATE_FOCUSED);
        kb_hide();
        start_stream();
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_remove_state(s_url_ta, LV_STATE_FOCUSED);
        kb_hide();
    }
}

static void screen_delete_cb(lv_event_t *e)
{
    (void)e;
    /* The timer must not outlive the image it writes into, and nobody is
     * watching the stream any more. */
    if (s_poll_timer != NULL) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    cam_stream_stop();
    s_img = NULL;
    s_state_val = NULL;
}

/* ------------------------------------------------------------------ */
/* Build / tick                                                       */
/* ------------------------------------------------------------------ */

void ui_build_camview(lv_obj_t *c)
{
    s_have_frame = false;
    s_dsc_i = 0;
    memset(s_dsc, 0, sizeof(s_dsc));

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    const lv_coord_t h = UI_CONTENT_H - 2 * UI_GUTTER;

    /* ---- video ---- */
    s_frame = ui_card(c, LV_PCT(100), h);
    lv_obj_set_flex_grow(s_frame, 1);
    lv_obj_set_style_bg_color(s_frame, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_frame, 0, 0);
    lv_obj_add_event_cb(s_frame, screen_delete_cb, LV_EVENT_DELETE, NULL);

    s_img = lv_image_create(s_frame);
    lv_obj_center(s_img);

    s_placeholder = ui_label(s_frame, LV_SYMBOL_IMAGE "  No video yet",
                             &lv_font_montserrat_20, UI_COL_TEXT_DIM);
    lv_obj_center(s_placeholder);

    /* ---- side panel ---- */
    lv_obj_t *side = ui_card_titled(c, 340, h, "MJPEG stream");

    s_url_ta = lv_textarea_create(side);
    lv_obj_set_width(s_url_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_url_ta, true);
    lv_textarea_set_max_length(s_url_ta, CAM_STREAM_URL_LEN - 1);
    lv_textarea_set_placeholder_text(s_url_ta, "http://<camera>:81/stream");
    lv_obj_set_style_bg_color(s_url_ta, UI_COL_CARD_ALT, 0);
    lv_obj_set_style_border_color(s_url_ta, UI_COL_BORDER, 0);
    lv_obj_set_style_text_color(s_url_ta, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_url_ta, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_url_ta, ta_cb, LV_EVENT_ALL, NULL);

    char url[CAM_STREAM_URL_LEN];
    cam_stream_load_url(url, sizeof(url));
    lv_textarea_set_text(s_url_ta, url);

    lv_obj_t *btns = lv_obj_create(side);
    lv_obj_set_size(btns, LV_PCT(100), 52);
    lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btns, 0, 0);
    lv_obj_set_style_pad_all(btns, 0, 0);
    lv_obj_clear_flag(btns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *run = ui_button(btns, LV_SYMBOL_PLAY "  Connect", UI_COL_ACCENT, run_cb, NULL);
    lv_obj_set_width(run, 190);
    lv_obj_align(run, LV_ALIGN_LEFT_MID, 0, 0);
    s_run_lbl = lv_obj_get_child(run, 0);

    lv_obj_t *fit = ui_button(btns, s_fit ? "Fit" : "1:1", UI_COL_CARD_ALT, fit_cb, NULL);
    lv_obj_set_width(fit, 90);
    lv_obj_align(fit, LV_ALIGN_RIGHT_MID, 0, 0);
    s_fit_lbl = lv_obj_get_child(fit, 0);

    s_state_val = ui_kv_row(side, LV_SYMBOL_WIFI, "State", "--");
    s_res_val   = ui_kv_row(side, LV_SYMBOL_IMAGE, "Resolution", "--");
    s_fps_val   = ui_kv_row(side, LV_SYMBOL_REFRESH, "Shown", "--");
    s_size_val  = ui_kv_row(side, LV_SYMBOL_DOWNLOAD, "JPEG", "--");
    s_drop_val  = ui_kv_row(side, LV_SYMBOL_WARNING, "Dropped", "--");

    s_msg = ui_label(side, "", &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_set_width(s_msg, LV_PCT(100));
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);

    /* ---- on-screen keyboard for the URL ---- */
    s_kb = lv_keyboard_create(c);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_kb, LV_PCT(100), 300);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_cb, LV_EVENT_ALL, NULL);

    s_poll_timer = lv_timer_create(poll_timer_cb, FRAME_POLL_MS, NULL);

    start_stream();
}

void ui_tick_camview(void)
{
    if (s_state_val == NULL) {
        return;
    }

    cam_stream_stats_t st;
    cam_stream_get_stats(&st);

    static const char *const k_state_text[] = {
        [CAM_STREAM_IDLE]       = "Stopped",
        [CAM_STREAM_NO_NETWORK] = "No network",
        [CAM_STREAM_CONNECTING] = "Connecting",
        [CAM_STREAM_STREAMING]  = "Streaming",
        [CAM_STREAM_ERROR]      = "Retrying",
    };
    lv_color_t col = UI_COL_TEXT;
    switch (st.state) {
    case CAM_STREAM_STREAMING:  col = UI_COL_GREEN; break;
    case CAM_STREAM_CONNECTING: col = UI_COL_AMBER; break;
    case CAM_STREAM_ERROR:
    case CAM_STREAM_NO_NETWORK: col = UI_COL_RED;   break;
    default:                    col = UI_COL_TEXT_DIM; break;
    }
    lv_label_set_text(s_state_val, k_state_text[st.state]);
    lv_obj_set_style_text_color(s_state_val, col, 0);

    char buf[48];
    if (st.width > 0) {
        lv_snprintf(buf, sizeof(buf), "%u x %u", st.width, st.height);
    } else {
        lv_snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_res_val, buf);

    /* Frame rate as actually drawn, which is what the viewer sees. */
    const uint32_t now = lv_tick_get();
    const uint32_t dt = now - s_fps_mark_tick;
    if (dt >= 1000) {
        const uint32_t df = s_shown - s_fps_mark_frames;
        lv_snprintf(buf, sizeof(buf), "%u.%u fps",
                    (unsigned)(df * 1000 / dt), (unsigned)((df * 10000 / dt) % 10));
        lv_label_set_text(s_fps_val, buf);
        s_fps_mark_frames = s_shown;
        s_fps_mark_tick = now;
    }

    if (st.last_jpeg_len > 0) {
        ui_format_bytes(st.last_jpeg_len, buf, sizeof(buf));
        lv_label_set_text(s_size_val, buf);
    } else {
        lv_label_set_text(s_size_val, "--");
    }

    lv_snprintf(buf, sizeof(buf), "%u  (bad %u)", (unsigned)st.dropped, (unsigned)st.errors);
    lv_label_set_text(s_drop_val, buf);

    if (st.state == CAM_STREAM_NO_NETWORK) {
        lv_label_set_text(s_msg,
                          "No network interface is up. Wi-Fi is on the ESP32-C6 "
                          "co-processor, which this firmware does not bring up yet, "
                          "so the camera cannot be reached.");
    } else {
        lv_label_set_text(s_msg, st.message);
    }

    const bool running = cam_stream_is_running();
    lv_label_set_text(s_run_lbl, running ? LV_SYMBOL_STOP "  Stop" : LV_SYMBOL_PLAY "  Connect");
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_run_lbl),
                              running ? UI_COL_RED : UI_COL_ACCENT, 0);
}
