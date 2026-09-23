/*
 * Remote camera viewer - MJPEG over HTTP client and JPEG decoder.
 *
 * Wire format (ESP32-CAM CameraWebServer and most IP cameras):
 *
 *   HTTP/1.1 200 OK
 *   Content-Type: multipart/x-mixed-replace;boundary=<b>
 *
 *   --<b>\r\n
 *   Content-Type: image/jpeg\r\n
 *   Content-Length: 12345\r\n
 *   \r\n
 *   <12345 bytes of JPEG>\r\n
 *   --<b>\r\n ...
 *
 * esp_http_client only exposes response headers through its event callback,
 * so the boundary string is not used. Each part is found by its JPEG SOI
 * marker instead; if the part headers carried a Content-Length that decides
 * where the part ends, otherwise the EOI marker does.
 *
 * Decoding uses the TJpgDec copy that LVGL already vendors (LV_USE_TJPGD), so
 * no extra component is needed. It handles baseline JPEG, which is what
 * camera sensors produce.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cam_stream.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "libs/tjpgd/tjpgd.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "cam_stream";

#define RX_CAP           (512 * 1024)    /* biggest JPEG part we accept */
#define RX_CHUNK         (16 * 1024)
#define MAX_PIXELS       (1920 * 1080)
#define HTTP_TIMEOUT_MS  1000             /* also bounds how long stop takes */
#define STALL_MS         5000
#define RETRY_MS         2000
#define TASK_STACK       6144
#define TASK_PRIO        3

#define NVS_NS           "camview"
#define NVS_KEY_URL      "url"

typedef struct {
    uint16_t *px;
    uint16_t  w, h;
    size_t    cap;      /* bytes allocated at px */
} frame_buf_t;

/* Guarded by s_lock. */
static SemaphoreHandle_t  s_lock;
static TaskHandle_t       s_task;
static char               s_url[CAM_STREAM_URL_LEN];
static volatile bool      s_run;
static volatile uint32_t  s_gen;          /* bumped on every start/stop */
static cam_stream_stats_t s_stats;

/*
 * Frame hand-over. s_buf[s_front] belongs to the UI. s_buf[!s_front] belongs
 * to the task while s_ready is false; once the task sets s_ready it may not
 * touch either buffer until cam_stream_take_frame() swaps them and clears it.
 */
static frame_buf_t s_buf[2];
static int         s_front;
static bool        s_ready;

/* Task-private receive state. */
static uint8_t *s_rx;
static size_t   s_rx_len;
static size_t   s_eoi_scan;    /* resume offset for the EOI search */
static uint8_t  s_jd_work[4096];

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool still_wanted(uint32_t gen)
{
    return s_run && s_gen == gen;
}

static void set_state(uint32_t gen, cam_stream_state_t st, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void set_state(uint32_t gen, cam_stream_state_t st, const char *fmt, ...)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* A stale session must not overwrite what start/stop just set. */
    if (s_gen == gen) {
        s_stats.state = st;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_stats.message, sizeof(s_stats.message), fmt, ap);
        va_end(ap);
    }
    xSemaphoreGive(s_lock);
}

static bool network_up(void)
{
    /* Without a default netif the TCP/IP stack may not even be initialised,
     * so no socket call is safe. Check before touching esp_http_client. */
    esp_netif_t *nif = esp_netif_get_default_netif();
    if (nif == NULL || !esp_netif_is_netif_up(nif)) {
        return false;
    }
    esp_netif_ip_info_t ip;
    return esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static const uint8_t *find2(const uint8_t *p, size_t len, uint8_t a, uint8_t b)
{
    for (size_t i = 0; i + 1 < len; i++) {
        if (p[i] == a && p[i + 1] == b) {
            return p + i;
        }
    }
    return NULL;
}

/** Content-Length from the part headers in hdr[0..len), or 0 if absent. */
static size_t part_content_length(const uint8_t *hdr, size_t len)
{
    char text[512];
    if (len >= sizeof(text)) {
        hdr += len - (sizeof(text) - 1);    /* the headers sit just before SOI */
        len = sizeof(text) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        text[i] = (char)tolower(hdr[i]);
    }
    text[len] = '\0';

    const char *p = strstr(text, "content-length:");
    if (p == NULL) {
        return 0;
    }
    return (size_t)strtoul(p + strlen("content-length:"), NULL, 10);
}

/* ------------------------------------------------------------------ */
/* JPEG decode                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *src;
    size_t         len;
    size_t         pos;
    frame_buf_t   *fb;
} jpg_io_t;

static size_t jd_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jpg_io_t *io = jd->device;
    if (n > io->len - io->pos) {
        n = io->len - io->pos;
    }
    if (buf != NULL) {
        memcpy(buf, io->src + io->pos, n);
    }
    io->pos += n;
    return n;
}

static int jd_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpg_io_t *io = jd->device;
    frame_buf_t *fb = io->fb;
    /* LVGL's TJpgDec emits 24-bit pixels in B, G, R byte order. */
    const uint8_t *s = bitmap;

    for (unsigned y = r->top; y <= r->bottom; y++) {
        uint16_t *d = fb->px + (size_t)y * fb->w + r->left;
        for (unsigned x = r->left; x <= r->right; x++) {
            *d++ = (uint16_t)(((s[2] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[0] >> 3));
            s += 3;
        }
    }
    return 1;
}

static bool decode_jpeg(frame_buf_t *fb, const uint8_t *jpg, size_t len)
{
    jpg_io_t io = { .src = jpg, .len = len, .pos = 0, .fb = fb };
    JDEC jd;

    JRESULT rc = jd_prepare(&jd, jd_in, s_jd_work, sizeof(s_jd_work), &io);
    if (rc != JDR_OK) {
        ESP_LOGD(TAG, "jd_prepare: %d", rc);
        return false;
    }
    if ((size_t)jd.width * jd.height > MAX_PIXELS) {
        ESP_LOGW(TAG, "frame %ux%u too large", jd.width, jd.height);
        return false;
    }

    const size_t need = (size_t)jd.width * jd.height * sizeof(uint16_t);
    if (fb->cap < need) {
        heap_caps_free(fb->px);
        fb->px = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
        fb->cap = fb->px != NULL ? need : 0;
        if (fb->px == NULL) {
            ESP_LOGE(TAG, "no PSRAM for a %ux%u frame", jd.width, jd.height);
            return false;
        }
    }
    fb->w = jd.width;
    fb->h = jd.height;

    rc = jd_decomp(&jd, jd_out, 0);
    if (rc != JDR_OK) {
        ESP_LOGD(TAG, "jd_decomp: %d", rc);
        return false;
    }
    return true;
}

static void handle_jpeg(uint32_t gen, const uint8_t *jpg, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool busy = s_ready;
    frame_buf_t *back = &s_buf[!s_front];
    if (s_gen == gen) {
        s_stats.last_jpeg_len = len;
        if (busy) {
            s_stats.dropped++;
        }
    }
    xSemaphoreGive(s_lock);

    if (busy) {
        return;     /* the UI has not picked up the previous frame yet */
    }

    /* The back buffer is ours while s_ready is false, so decode unlocked. */
    const bool ok = decode_jpeg(back, jpg, len);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_gen == gen) {
        if (ok) {
            s_ready = true;
            s_stats.frames++;
            s_stats.width = back->w;
            s_stats.height = back->h;
        } else {
            s_stats.errors++;
        }
    }
    xSemaphoreGive(s_lock);
}

/* ------------------------------------------------------------------ */
/* Multipart framing                                                  */
/* ------------------------------------------------------------------ */

static void rx_consume(size_t n)
{
    memmove(s_rx, s_rx + n, s_rx_len - n);
    s_rx_len -= n;
    s_eoi_scan = 0;
}

/** Pull every complete JPEG part out of s_rx. */
static void rx_parse(uint32_t gen)
{
    for (;;) {
        const uint8_t *soi = find2(s_rx, s_rx_len, 0xFF, 0xD8);
        if (soi == NULL) {
            /* Only part headers so far. Keep a trailing 0xFF in case it is
             * the first half of an SOI split across two reads. */
            if (s_rx_len > 4096) {
                rx_consume(s_rx_len - 1);
            }
            return;
        }

        const size_t start = (size_t)(soi - s_rx);
        const size_t avail = s_rx_len - start;
        size_t jpeg_len = part_content_length(s_rx, start);

        if (jpeg_len >= 4) {
            if (jpeg_len > RX_CAP - start) {
                rx_consume(start + 2);     /* cannot ever fit; resync */
                continue;
            }
            if (avail < jpeg_len) {
                return;                    /* wait for the rest */
            }
        } else {
            const size_t from = s_eoi_scan > 2 ? s_eoi_scan : 2;
            const uint8_t *eoi = find2(soi + from, avail - from, 0xFF, 0xD9);
            if (eoi == NULL) {
                s_eoi_scan = avail > 1 ? avail - 1 : 2;
                return;
            }
            jpeg_len = (size_t)(eoi - soi) + 2;
        }

        handle_jpeg(gen, soi, jpeg_len);
        rx_consume(start + jpeg_len);
    }
}

/* ------------------------------------------------------------------ */
/* Session                                                            */
/* ------------------------------------------------------------------ */

/** Run one HTTP session until it fails or is no longer wanted. */
static void run_session(uint32_t gen, const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        set_state(gen, CAM_STREAM_ERROR, "Invalid URL");
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        set_state(gen, CAM_STREAM_ERROR, "Connect failed (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        set_state(gen, CAM_STREAM_ERROR, "No HTTP response");
        goto out;
    }
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        set_state(gen, CAM_STREAM_ERROR, "HTTP %d from camera", status);
        goto out;
    }

    set_state(gen, CAM_STREAM_STREAMING, "%s", url);
    s_rx_len = 0;
    s_eoi_scan = 0;
    int64_t last_data = esp_timer_get_time();

    while (still_wanted(gen)) {
        if (s_rx_len == RX_CAP) {
            ESP_LOGW(TAG, "no complete JPEG in %d bytes, resyncing", RX_CAP);
            s_rx_len = 0;
            s_eoi_scan = 0;
        }
        size_t want = RX_CAP - s_rx_len;
        if (want > RX_CHUNK) {
            want = RX_CHUNK;
        }

        const int n = esp_http_client_read(client, (char *)s_rx + s_rx_len, (int)want);
        if (n > 0) {
            s_rx_len += (size_t)n;
            last_data = esp_timer_get_time();
            rx_parse(gen);
            continue;
        }

        /* 0 or -ESP_ERR_HTTP_EAGAIN is a read timeout on some IDF versions
         * and a clean close on others; the stall timer tells them apart. */
        if (n < 0 && n != -ESP_ERR_HTTP_EAGAIN) {
            set_state(gen, CAM_STREAM_ERROR, "Connection lost");
            break;
        }
        if (esp_http_client_is_complete_data_received(client) && n == 0) {
            set_state(gen, CAM_STREAM_ERROR, "Camera closed the stream");
            break;
        }
        if (esp_timer_get_time() - last_data > STALL_MS * 1000LL) {
            set_state(gen, CAM_STREAM_ERROR, "No data for %d s", STALL_MS / 1000);
            break;
        }
        /* A closed socket can return 0 immediately; do not spin on it. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

out:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void stream_task(void *arg)
{
    (void)arg;
    char url[CAM_STREAM_URL_LEN];

    for (;;) {
        if (!s_run) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        const uint32_t gen = s_gen;
        snprintf(url, sizeof(url), "%s", s_url);
        xSemaphoreGive(s_lock);

        if (!network_up()) {
            set_state(gen, CAM_STREAM_NO_NETWORK, "No network interface is up");
        } else {
            set_state(gen, CAM_STREAM_CONNECTING, "%s", url);
            run_session(gen, url);
        }

        /* Back off before retrying; a start/stop cuts the wait short. */
        if (still_wanted(gen)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RETRY_MS));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

static bool ensure_task(void)
{
    if (s_task != NULL) {
        return true;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (s_rx == NULL) {
        s_rx = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM);
    }
    if (s_lock == NULL || s_rx == NULL) {
        ESP_LOGE(TAG, "out of memory");
        return false;
    }
    /* The task lives for the rest of the run and parks when idle, so start
     * and stop never have to wait for it to exit. */
    if (xTaskCreate(stream_task, "cam_stream", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "could not create task");
        return false;
    }
    return true;
}

void cam_stream_start(const char *url)
{
    if (!ensure_task()) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_url, sizeof(s_url), "%s", url);
    s_gen++;
    s_run = true;
    s_ready = false;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.state = CAM_STREAM_CONNECTING;
    snprintf(s_stats.message, sizeof(s_stats.message), "%s", url);
    xSemaphoreGive(s_lock);

    xTaskNotifyGive(s_task);
}

void cam_stream_stop(void)
{
    if (s_task == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_gen++;
    s_run = false;
    s_stats.state = CAM_STREAM_IDLE;
    snprintf(s_stats.message, sizeof(s_stats.message), "Stopped");
    xSemaphoreGive(s_lock);

    xTaskNotifyGive(s_task);
}

bool cam_stream_is_running(void)
{
    return s_run;
}

void cam_stream_get_stats(cam_stream_stats_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        snprintf(out->message, sizeof(out->message), "Stopped");
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}

bool cam_stream_take_frame(const uint16_t **pixels, uint16_t *w, uint16_t *h)
{
    if (s_lock == NULL) {
        return false;
    }
    bool got = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_ready) {
        s_front = !s_front;
        s_ready = false;
        *pixels = s_buf[s_front].px;
        *w = s_buf[s_front].w;
        *h = s_buf[s_front].h;
        got = true;
    }
    xSemaphoreGive(s_lock);
    return got;
}

void cam_stream_load_url(char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", CONFIG_APP_CAM_STREAM_URL);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = out_len;
    if (nvs_get_str(h, NVS_KEY_URL, out, &len) != ESP_OK) {
        snprintf(out, out_len, "%s", CONFIG_APP_CAM_STREAM_URL);
    }
    nvs_close(h);
}

void cam_stream_save_url(const char *url)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, NVS_KEY_URL, url) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}
