/*
 * Remote camera viewer: pulls a multipart MJPEG stream over HTTP
 * (e.g. an ESP32-CAM's  GET :81/stream) and decodes it to RGB565 frames.
 *
 * One background task does the network I/O and JPEG decode. Frames are handed
 * to the UI through a two-buffer swap that only happens on the LVGL side, so
 * the task never writes into a buffer LVGL may be drawing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_STREAM_URL_LEN 128

typedef enum {
    CAM_STREAM_IDLE = 0,
    CAM_STREAM_NO_NETWORK,  /**< no network interface is up */
    CAM_STREAM_CONNECTING,
    CAM_STREAM_STREAMING,
    CAM_STREAM_ERROR,       /**< will retry after a short back-off */
} cam_stream_state_t;

typedef struct {
    cam_stream_state_t state;
    char     message[96];   /**< human readable detail for the current state */
    uint32_t frames;        /**< frames decoded since cam_stream_start() */
    uint32_t dropped;       /**< frames received while the UI was still behind */
    uint32_t errors;        /**< JPEG parts that failed to decode */
    uint32_t last_jpeg_len; /**< size of the last JPEG part, bytes */
    uint16_t width, height; /**< size of the last decoded frame */
} cam_stream_stats_t;

/** Start (or restart) streaming from `url`. Returns immediately. */
void cam_stream_start(const char *url);

/** Stop streaming. Returns immediately; the connection closes in the
 *  background within about a second. */
void cam_stream_stop(void);

bool cam_stream_is_running(void);

void cam_stream_get_stats(cam_stream_stats_t *out);

/**
 * Take the newest decoded frame, if there is one the caller has not seen.
 * Call from the LVGL task only. The returned pixels stay valid and unchanged
 * until the next call that returns true.
 */
bool cam_stream_take_frame(const uint16_t **pixels, uint16_t *w, uint16_t *h);

/** URL persisted in NVS, or CONFIG_APP_CAM_STREAM_URL if none was saved. */
void cam_stream_load_url(char *out, size_t out_len);
void cam_stream_save_url(const char *url);

#ifdef __cplusplus
}
#endif
