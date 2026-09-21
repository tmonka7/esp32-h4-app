/*
 * Video, Music, Photo and Camera screens.
 *
 * Photo and Video decode real files from the TF card through LVGL image
 * decoders (TJpgDec for JPEG, LodePNG for PNG). Music plays real 16-bit PCM
 * WAV files through the ES8311. Camera is honest about needing a sensor on
 * the CSI connector, which this board does not ship with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app_wav.h"
#include "bsp/jc8012p4a1.h"

#define MEDIA_MAX_FILES 64
#define MEDIA_NAME_LEN  96
#define MEDIA_PATH_LEN  256

/* LVGL file-system letter, must match CONFIG_LV_FS_STDIO_LETTER ('S'). */
#define LV_FS_LETTER "S:"

static char s_files[MEDIA_MAX_FILES][MEDIA_NAME_LEN];
static int  s_file_count;
static char s_dir[MEDIA_PATH_LEN];

static bool has_ext(const char *name, const char *const *exts)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    for (int i = 0; exts[i] != NULL; i++) {
        if (strcasecmp(dot, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Collect files with the given extensions from the first of `dirs` that
 * exists. Results land in s_files / s_file_count and the chosen directory in
 * s_dir.
 */
static int scan_media(const char *const *dirs, const char *const *exts)
{
    s_file_count = 0;
    s_dir[0] = '\0';

    if (!bsp_sdcard_is_mounted()) {
        return 0;
    }

    for (int di = 0; dirs[di] != NULL; di++) {
        DIR *d = opendir(dirs[di]);
        if (d == NULL) {
            continue;
        }

        strncpy(s_dir, dirs[di], sizeof(s_dir) - 1);
        s_dir[sizeof(s_dir) - 1] = '\0';

        struct dirent *ent;
        while (s_file_count < MEDIA_MAX_FILES && (ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.' || !has_ext(ent->d_name, exts)) {
                continue;
            }
            strncpy(s_files[s_file_count], ent->d_name, MEDIA_NAME_LEN - 1);
            s_files[s_file_count][MEDIA_NAME_LEN - 1] = '\0';
            s_file_count++;
        }
        closedir(d);

        if (s_file_count > 0) {
            break;
        }
    }
    return s_file_count;
}

/* ================================================================== */
/* Photo                                                              */
/* ================================================================== */

static const char *const k_photo_dirs[] = {
    BSP_SD_MOUNT_POINT "/Pictures", BSP_SD_MOUNT_POINT "/DCIM",
    BSP_SD_MOUNT_POINT, NULL
};
static const char *const k_photo_exts[] = { ".jpg", ".jpeg", ".png", ".bmp", NULL };

static lv_obj_t *s_photo_img;
static lv_obj_t *s_photo_name;
static int       s_photo_index;

static void photo_load(int index)
{
    if (s_file_count == 0 || s_photo_img == NULL) {
        return;
    }
    if (index < 0) {
        index = s_file_count - 1;
    } else if (index >= s_file_count) {
        index = 0;
    }
    s_photo_index = index;

    static char lv_path[MEDIA_PATH_LEN + 4];
    snprintf(lv_path, sizeof(lv_path), LV_FS_LETTER "%s/%s", s_dir, s_files[index]);
    lv_image_set_src(s_photo_img, lv_path);

    char caption[MEDIA_NAME_LEN + 24];
    snprintf(caption, sizeof(caption), "%s        %d/%d",
             s_files[index], index + 1, s_file_count);
    lv_label_set_text(s_photo_name, caption);
}

static void photo_prev_cb(lv_event_t *e)
{
    (void)e;
    photo_load(s_photo_index - 1);
}

static void photo_next_cb(lv_event_t *e)
{
    (void)e;
    photo_load(s_photo_index + 1);
}

void ui_build_photo(lv_obj_t *c)
{
    s_photo_img = NULL;
    s_photo_name = NULL;
    s_photo_index = 0;

    if (scan_media(k_photo_dirs, k_photo_exts) == 0) {
        ui_notice(c, LV_SYMBOL_IMAGE, "No images found",
                  "Put .jpg, .png or .bmp files in /Pictures, /DCIM or the root "
                  "of the TF card. Baseline JPEG and PNG are decoded on the "
                  "device; progressive JPEG is not supported.");
        return;
    }

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);

    lv_obj_t *frame = ui_card(c, LV_PCT(100), 0);
    lv_obj_set_flex_grow(frame, 1);
    lv_obj_set_style_bg_color(frame, lv_color_black(), 0);
    lv_obj_set_style_pad_all(frame, 0, 0);

    s_photo_img = lv_image_create(frame);
    lv_obj_center(s_photo_img);
    lv_image_set_inner_align(s_photo_img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_size(s_photo_img, LV_PCT(100), LV_PCT(100));

    lv_obj_t *bar = lv_obj_create(c);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev = ui_button(bar, LV_SYMBOL_LEFT, UI_COL_CARD_ALT, photo_prev_cb, NULL);
    lv_obj_set_width(prev, 72);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *next = ui_button(bar, LV_SYMBOL_RIGHT, UI_COL_CARD_ALT, photo_next_cb, NULL);
    lv_obj_set_width(next, 72);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);

    s_photo_name = ui_label(bar, "", &lv_font_montserrat_16, UI_COL_TEXT);
    lv_obj_align(s_photo_name, LV_ALIGN_CENTER, 0, 0);

    photo_load(0);
}

/* ================================================================== */
/* Video - JPEG frame-sequence player                                 */
/* ================================================================== */
/*
 * There is no video codec in this build: ESP-IDF v5.3 has no driver for the
 * ESP32-P4 hardware JPEG decoder (that arrived in v5.4) and no container
 * parser is vendored. What does work end to end is playing a directory of
 * numbered JPEG frames, which exercises exactly the same decode-and-blit path
 * a real player would use.
 */

static const char *const k_video_dirs[] = {
    BSP_SD_MOUNT_POINT "/Movies", BSP_SD_MOUNT_POINT "/Video", NULL
};
static const char *const k_video_exts[] = { ".jpg", ".jpeg", NULL };

static lv_obj_t  *s_vid_img;
static lv_obj_t  *s_vid_status;
static lv_obj_t  *s_vid_bar;
static lv_timer_t *s_vid_timer;
static int        s_vid_frame;

static void vid_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_vid_img == NULL || s_file_count == 0) {
        return;
    }

    s_vid_frame = (s_vid_frame + 1) % s_file_count;

    static char lv_path[MEDIA_PATH_LEN + 4];
    snprintf(lv_path, sizeof(lv_path), LV_FS_LETTER "%s/%s", s_dir, s_files[s_vid_frame]);
    lv_image_set_src(s_vid_img, lv_path);

    lv_bar_set_value(s_vid_bar, (s_vid_frame * 100) / s_file_count, LV_ANIM_OFF);

    char buf[64];
    snprintf(buf, sizeof(buf), "frame %d / %d", s_vid_frame + 1, s_file_count);
    lv_label_set_text(s_vid_status, buf);
}

static void vid_playpause_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);

    if (s_vid_timer == NULL) {
        return;
    }
    if (lv_timer_get_paused(s_vid_timer)) {
        lv_timer_resume(s_vid_timer);
        lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
    } else {
        lv_timer_pause(s_vid_timer);
        lv_label_set_text(lbl, LV_SYMBOL_PLAY);
    }
}

static void vid_delete_cb(lv_event_t *e)
{
    (void)e;
    /* The timer must not outlive the image it writes into. */
    if (s_vid_timer != NULL) {
        lv_timer_delete(s_vid_timer);
        s_vid_timer = NULL;
    }
    s_vid_img = NULL;
}

void ui_build_video(lv_obj_t *c)
{
    s_vid_img = NULL;
    s_vid_status = NULL;
    s_vid_bar = NULL;
    s_vid_timer = NULL;
    s_vid_frame = 0;

    if (scan_media(k_video_dirs, k_video_exts) == 0) {
        ui_notice(c, LV_SYMBOL_VIDEO, "No playable frames found",
                  "This build has no video codec: ESP-IDF v5.3 predates the "
                  "ESP32-P4 hardware JPEG driver and no container parser is "
                  "vendored. Drop a numbered sequence of baseline .jpg frames "
                  "into /Movies or /Video on the TF card and they will play "
                  "here at 10 fps.");
        return;
    }

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);

    lv_obj_t *frame = ui_card(c, LV_PCT(100), 0);
    lv_obj_set_flex_grow(frame, 1);
    lv_obj_set_style_bg_color(frame, lv_color_black(), 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_add_event_cb(frame, vid_delete_cb, LV_EVENT_DELETE, NULL);

    s_vid_img = lv_image_create(frame);
    lv_obj_set_size(s_vid_img, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(s_vid_img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(s_vid_img);

    s_vid_bar = lv_bar_create(c);
    lv_obj_set_size(s_vid_bar, LV_PCT(100), 6);
    lv_bar_set_range(s_vid_bar, 0, 100);
    lv_obj_set_style_bg_color(s_vid_bar, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vid_bar, UI_COL_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *bar = lv_obj_create(c);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pp = ui_button(bar, LV_SYMBOL_PAUSE, UI_COL_ACCENT, vid_playpause_cb, NULL);
    lv_obj_set_width(pp, 80);
    lv_obj_align(pp, LV_ALIGN_LEFT_MID, 0, 0);

    s_vid_status = ui_label(bar, "", &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(s_vid_status, LV_ALIGN_LEFT_MID, 100, 0);

    lv_obj_t *src = ui_label(bar, s_dir, &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_align(src, LV_ALIGN_RIGHT_MID, 0, 0);

    s_vid_timer = lv_timer_create(vid_timer_cb, 100, NULL);   /* 10 fps */
    lv_timer_ready(s_vid_timer);
}

/* ================================================================== */
/* Music                                                              */
/* ================================================================== */

static const char *const k_music_dirs[] = {
    BSP_SD_MOUNT_POINT "/Music", BSP_SD_MOUNT_POINT, NULL
};
static const char *const k_music_exts[] = { ".wav", NULL };

static lv_obj_t *s_mus_title;
static lv_obj_t *s_mus_time;
static lv_obj_t *s_mus_bar;
static lv_obj_t *s_mus_play_lbl;
static int       s_mus_index;

static void music_play_index(int index)
{
    if (s_file_count == 0) {
        return;
    }
    if (index < 0) {
        index = s_file_count - 1;
    } else if (index >= s_file_count) {
        index = 0;
    }
    s_mus_index = index;

    char path[MEDIA_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", s_dir, s_files[index]);
    app_wav_play(path);

    if (s_mus_title != NULL) {
        lv_label_set_text(s_mus_title, s_files[index]);
    }
}

static void music_row_cb(lv_event_t *e)
{
    music_play_index((int)(intptr_t)lv_event_get_user_data(e));
}

static void music_playpause_cb(lv_event_t *e)
{
    (void)e;
    if (app_wav_is_playing()) {
        app_wav_stop();
    } else {
        music_play_index(s_mus_index);
    }
}

static void music_prev_cb(lv_event_t *e)
{
    (void)e;
    music_play_index(s_mus_index - 1);
}

static void music_next_cb(lv_event_t *e)
{
    (void)e;
    music_play_index(s_mus_index + 1);
}

void ui_build_music(lv_obj_t *c)
{
    s_mus_title = NULL;
    s_mus_time = NULL;
    s_mus_bar = NULL;
    s_mus_play_lbl = NULL;

    if (!bsp_audio_is_ready()) {
        ui_notice(c, LV_SYMBOL_MUTE, "Audio codec not available",
                  "No ES8311 answered at I2C address 0x18, so nothing can be "
                  "played. Check the codec supply and the shared I2C bus on "
                  "GPIO7/GPIO8.");
        return;
    }

    if (scan_media(k_music_dirs, k_music_exts) == 0) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                 "Put 16-bit PCM .wav files at %d Hz in /Music or the root of "
                 "the TF card. Other sample rates play at the wrong pitch "
                 "because the codec is opened once at a fixed rate.",
                 CONFIG_BSP_AUDIO_SAMPLE_RATE);
        ui_notice(c, LV_SYMBOL_AUDIO, "No WAV files found", detail);
        return;
    }

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(c, UI_GUTTER, 0);

    /* ---- now playing ---- */
    lv_obj_t *left = ui_card(c, 440, UI_CONTENT_H - 2 * UI_GUTTER);

    lv_obj_t *art = lv_obj_create(left);
    lv_obj_set_size(art, 180, 180);
    lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(art, UI_COL_PINK, 0);
    lv_obj_set_style_radius(art, 20, 0);
    lv_obj_set_style_border_width(art, 0, 0);
    lv_obj_clear_flag(art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *note = ui_label(art, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, UI_COL_TEXT);
    lv_obj_center(note);

    s_mus_title = ui_label(left, s_files[0], &lv_font_montserrat_20, UI_COL_TEXT);
    lv_obj_set_width(s_mus_title, LV_PCT(100));
    lv_label_set_long_mode(s_mus_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_mus_title, LV_ALIGN_TOP_MID, 0, 208);

    s_mus_bar = lv_bar_create(left);
    lv_obj_set_size(s_mus_bar, LV_PCT(100), 6);
    lv_obj_align(s_mus_bar, LV_ALIGN_TOP_MID, 0, 256);
    lv_bar_set_range(s_mus_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_mus_bar, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_mus_bar, UI_COL_PINK, LV_PART_INDICATOR);

    s_mus_time = ui_label(left, "00:00 / 00:00", &lv_font_montserrat_14, UI_COL_TEXT_DIM);
    lv_obj_align(s_mus_time, LV_ALIGN_TOP_MID, 0, 272);

    lv_obj_t *prev = ui_button(left, LV_SYMBOL_PREV, UI_COL_CARD_ALT, music_prev_cb, NULL);
    lv_obj_set_width(prev, 64);
    lv_obj_align(prev, LV_ALIGN_TOP_MID, -110, 312);

    lv_obj_t *pp = ui_button(left, LV_SYMBOL_PLAY, UI_COL_PINK, music_playpause_cb, NULL);
    lv_obj_set_size(pp, 72, 56);
    lv_obj_align(pp, LV_ALIGN_TOP_MID, 0, 308);
    s_mus_play_lbl = lv_obj_get_child(pp, 0);

    lv_obj_t *next = ui_button(left, LV_SYMBOL_NEXT, UI_COL_CARD_ALT, music_next_cb, NULL);
    lv_obj_set_width(next, 64);
    lv_obj_align(next, LV_ALIGN_TOP_MID, 110, 312);

    /* ---- playlist ---- */
    lv_obj_t *right = ui_card_titled(c, LV_PCT(100), UI_CONTENT_H - 2 * UI_GUTTER, "Playlist");
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *list = lv_list_create(right);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);

    for (int i = 0; i < s_file_count; i++) {
        char row[MEDIA_NAME_LEN + 8];
        snprintf(row, sizeof(row), "%d. %s", i + 1, s_files[i]);
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_AUDIO, row);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        lv_obj_add_event_cb(btn, music_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void ui_tick_music(void)
{
    if (s_mus_time == NULL) {
        return;
    }

    const uint32_t pos = app_wav_position_ms() / 1000;
    const uint32_t dur = app_wav_duration_ms() / 1000;

    char buf[32];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u / %02u:%02u",
                (unsigned)(pos / 60), (unsigned)(pos % 60),
                (unsigned)(dur / 60), (unsigned)(dur % 60));
    lv_label_set_text(s_mus_time, buf);

    lv_bar_set_value(s_mus_bar, dur ? (int32_t)((pos * 1000) / dur) : 0, LV_ANIM_OFF);

    if (s_mus_play_lbl != NULL) {
        lv_label_set_text(s_mus_play_lbl,
                          app_wav_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
}

/* ================================================================== */
/* Camera                                                             */
/* ================================================================== */

void ui_build_camera(lv_obj_t *c)
{
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, UI_GUTTER, 0);

    /* The CSI connector (FPC5) shares the I2C bus for sensor control, so a
     * fitted sensor shows up as an extra address. These are the usual ones. */
    static const uint8_t sensor_addrs[] = { 0x3C, 0x30, 0x36, 0x21, 0x10 };
    int found = -1;
    for (size_t i = 0; i < sizeof(sensor_addrs); i++) {
        if (bsp_i2c_probe(sensor_addrs[i])) {
            found = sensor_addrs[i];
            break;
        }
    }

    if (found < 0) {
        ui_notice(c, LV_SYMBOL_EYE_OPEN, "No camera sensor detected",
                  "The board exposes a 15-pin MIPI-CSI connector (FPC5) but "
                  "ships without a sensor. Nothing answered on the camera I2C "
                  "addresses, so there is no live view to show.");
    } else {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "Something answered at I2C address 0x%02X on the camera bus. "
                 "Capture is not implemented in this build: it needs the "
                 "esp_cam_sensor and esp_video components, which are not "
                 "vendored here.", found);
        ui_notice(c, LV_SYMBOL_EYE_OPEN, "Sensor present, capture not built", detail);
    }

    lv_obj_t *info = ui_card_titled(c, LV_PCT(100), 260, "CSI connector");
    ui_kv_row(info, LV_SYMBOL_SETTINGS, "Interface", "MIPI-CSI, 2 data lanes + clock");
    ui_kv_row(info, LV_SYMBOL_SETTINGS, "Connector", "FPC5, 15-pin 0.3 mm");
    ui_kv_row(info, LV_SYMBOL_SETTINGS, "Control bus", "shared I2C0, SDA GPIO7 / SCL GPIO8");
    ui_kv_row(info, LV_SYMBOL_SETTINGS, "Spare sensor IO", "CSI_IO0, CSI_IO1");
}
