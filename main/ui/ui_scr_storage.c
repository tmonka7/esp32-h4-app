/*
 * SD Card and File Manager screens. Both read the real FAT volume mounted at
 * /sdcard; nothing here is mocked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/jc8012p4a1.h"
#include "esp_log.h"

#define PATH_MAX_LEN 256

static const char *TAG = "ui_storage";

/* ================================================================== */
/* Shared directory listing                                           */
/* ================================================================== */

typedef struct {
    char   name[128];
    bool   is_dir;
    size_t size;
    int    entries;   /* for directories */
} dir_entry_t;

/* Count the immediate children of a directory, so folder rows can show
 * "12 items" the way the reference design does. */
static int count_entries(const char *path)
{
    DIR *d = opendir(path);
    if (d == NULL) {
        return -1;
    }
    int n = 0;
    while (readdir(d) != NULL) {
        n++;
    }
    closedir(d);
    return n;
}

static int list_dir(const char *path, dir_entry_t *out, int max)
{
    DIR *d = opendir(path);
    if (d == NULL) {
        ESP_LOGW(TAG, "opendir(%s) failed", path);
        return -1;
    }

    int n = 0;
    struct dirent *ent;
    while (n < max && (ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;   /* skip "." and ".." and hidden entries */
        }

        char full[PATH_MAX_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }

        strncpy(out[n].name, ent->d_name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].is_dir  = S_ISDIR(st.st_mode);
        out[n].size    = (size_t)st.st_size;
        out[n].entries = out[n].is_dir ? count_entries(full) : 0;
        n++;
    }

    closedir(d);
    return n;
}

/* ================================================================== */
/* SD Card                                                            */
/* ================================================================== */

static void mount_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_sdcard_is_mounted()) {
        bsp_sdcard_unmount();
    } else {
        bsp_sdcard_mount();
    }
    ui_show(UI_SCR_SDCARD);
}

void ui_build_sdcard(lv_obj_t *c)
{
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, UI_GUTTER, 0);

    if (!bsp_sdcard_is_mounted()) {
        ui_notice(c, LV_SYMBOL_SD_CARD, "No TF card mounted",
                  "Insert a FAT16/FAT32/exFAT formatted card and tap Mount. "
                  "The slot is SDMMC slot 0: CLK GPIO43, CMD GPIO44, "
                  "D0-D3 GPIO39-42, card power from on-chip LDO channel 4.");
        ui_button(c, LV_SYMBOL_REFRESH "  Mount", UI_COL_ACCENT, mount_cb, NULL);
        return;
    }

    sdmmc_card_t *card = bsp_sdcard_get_card();

    /* ---- capacity header ---- */
    lv_obj_t *head = ui_card(c, LV_PCT(100), 150);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(head, 10, 0);

    lv_obj_t *title_row = lv_obj_create(head);
    lv_obj_set_size(title_row, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = ui_label(title_row, LV_SYMBOL_SD_CARD, &lv_font_montserrat_28, UI_COL_PURPLE);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nm = ui_label(title_row, card ? card->cid.name : "SD Card",
                            &lv_font_montserrat_20, UI_COL_TEXT);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 44, -10);

    uint64_t total = 0;
    uint64_t used = 0;
    char used_s[24] = "?";
    char total_s[24] = "?";
    int pct = 0;

    if (bsp_sdcard_usage(&total, &used) == ESP_OK && total > 0) {
        ui_format_bytes(used, used_s, sizeof(used_s));
        ui_format_bytes(total, total_s, sizeof(total_s));
        pct = (int)((used * 100) / total);
    }

    char cap[64];
    snprintf(cap, sizeof(cap), "Used: %s / %s", used_s, total_s);
    lv_obj_t *capl = ui_label(title_row, cap, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(capl, LV_ALIGN_LEFT_MID, 44, 12);

    lv_obj_t *bar = lv_bar_create(head);
    lv_obj_set_size(bar, LV_PCT(100), 12);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, UI_COL_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);

    /* ---- root listing ---- */
    lv_obj_t *list = lv_list_create(c);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_color(list, UI_COL_CARD, 0);
    lv_obj_set_style_border_color(list, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 14, 0);
    lv_obj_set_style_pad_all(list, 6, 0);

    static dir_entry_t entries[48];
    const int n = list_dir(BSP_SD_MOUNT_POINT, entries, 48);

    if (n <= 0) {
        lv_list_add_text(list, n == 0 ? "Card is empty" : "Cannot read the card");
    } else {
        for (int i = 0; i < n; i++) {
            char right[48];
            if (entries[i].is_dir) {
                snprintf(right, sizeof(right), "%d items", entries[i].entries);
            } else {
                ui_format_bytes(entries[i].size, right, sizeof(right));
            }

            char row[192];
            snprintf(row, sizeof(row), "%s    %s", entries[i].name, right);

            lv_obj_t *btn = lv_list_add_button(
                list, entries[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, row);
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        }
    }

    ui_button(c, LV_SYMBOL_EJECT "  Unmount", UI_COL_RED, mount_cb, NULL);
}

/* ================================================================== */
/* File Manager                                                       */
/* ================================================================== */

static char      s_cwd[PATH_MAX_LEN] = BSP_SD_MOUNT_POINT;
static lv_obj_t *s_files_list;
static lv_obj_t *s_files_path;

static void files_refresh(void);

static void files_open_cb(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);

    char next[PATH_MAX_LEN];
    snprintf(next, sizeof(next), "%s/%s", s_cwd, name);

    struct stat st;
    if (stat(next, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncpy(s_cwd, next, sizeof(s_cwd) - 1);
        s_cwd[sizeof(s_cwd) - 1] = '\0';
        files_refresh();
    }
}

static void files_up_cb(lv_event_t *e)
{
    (void)e;
    if (strcmp(s_cwd, BSP_SD_MOUNT_POINT) == 0) {
        return;
    }
    char *slash = strrchr(s_cwd, '/');
    if (slash != NULL && slash != s_cwd) {
        *slash = '\0';
    }
    files_refresh();
}

/* Names referenced by the row callbacks have to outlive the build call, so
 * they live in this table rather than on the stack. */
static char s_names[48][128];

static void files_refresh(void)
{
    if (s_files_list == NULL) {
        return;
    }

    lv_label_set_text(s_files_path, s_cwd);
    lv_obj_clean(s_files_list);

    static dir_entry_t entries[48];
    const int n = list_dir(s_cwd, entries, 48);

    if (n < 0) {
        lv_list_add_text(s_files_list, "Cannot read this directory");
        return;
    }
    if (n == 0) {
        lv_list_add_text(s_files_list, "Empty");
        return;
    }

    for (int i = 0; i < n; i++) {
        strncpy(s_names[i], entries[i].name, sizeof(s_names[i]) - 1);
        s_names[i][sizeof(s_names[i]) - 1] = '\0';

        char right[48];
        if (entries[i].is_dir) {
            snprintf(right, sizeof(right), "%d items", entries[i].entries);
        } else {
            ui_format_bytes(entries[i].size, right, sizeof(right));
        }

        char row[192];
        snprintf(row, sizeof(row), "%s    %s", entries[i].name, right);

        lv_obj_t *btn = lv_list_add_button(
            s_files_list, entries[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, row);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        if (entries[i].is_dir) {
            lv_obj_add_event_cb(btn, files_open_cb, LV_EVENT_CLICKED, s_names[i]);
        }
    }
}

void ui_build_files(lv_obj_t *c)
{
    s_files_list = NULL;
    s_files_path = NULL;

    if (!bsp_sdcard_is_mounted()) {
        ui_notice(c, LV_SYMBOL_DIRECTORY, "Nothing to browse",
                  "The only writable volume on this board is the TF card, and "
                  "no card is mounted. Mount one from the SD Card screen.");
        return;
    }

    /* A card may have been swapped since the last visit. */
    struct stat st;
    if (stat(s_cwd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        strncpy(s_cwd, BSP_SD_MOUNT_POINT, sizeof(s_cwd) - 1);
    }

    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);

    lv_obj_t *bar = lv_obj_create(c);
    lv_obj_set_size(bar, LV_PCT(100), 52);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *up = ui_button(bar, LV_SYMBOL_UP "  Up", UI_COL_CARD_ALT, files_up_cb, NULL);
    lv_obj_set_width(up, 110);
    lv_obj_align(up, LV_ALIGN_LEFT_MID, 0, 0);

    s_files_path = ui_label(bar, s_cwd, &lv_font_montserrat_16, UI_COL_TEXT_DIM);
    lv_obj_align(s_files_path, LV_ALIGN_LEFT_MID, 126, 0);

    s_files_list = lv_list_create(c);
    lv_obj_set_width(s_files_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_files_list, 1);
    lv_obj_set_style_bg_color(s_files_list, UI_COL_CARD, 0);
    lv_obj_set_style_border_color(s_files_list, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(s_files_list, 1, 0);
    lv_obj_set_style_radius(s_files_list, 14, 0);
    lv_obj_set_style_pad_all(s_files_list, 6, 0);

    files_refresh();
}
