/*
 * 16-bit PCM RIFF/WAVE playback.
 *
 * The ES8311 is opened once by the BSP at a fixed rate (see
 * CONFIG_BSP_AUDIO_SAMPLE_RATE). Files recorded at that rate play correctly;
 * anything else is reported rather than played back at the wrong pitch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_wav.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/jc8012p4a1.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_wav";

#define WAV_CHUNK_BYTES (4096)

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
} riff_header_t;

typedef struct __attribute__((packed)) {
    char     id[4];
    uint32_t size;
} chunk_header_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} fmt_chunk_t;

static TaskHandle_t   s_task;
static volatile bool  s_run;
static char           s_path[256];
static volatile uint32_t s_pos_ms;
static volatile uint32_t s_dur_ms;
static volatile uint32_t s_byte_rate;

/**
 * Walk the RIFF chunks to the data chunk. Returns the number of data bytes,
 * leaving the file positioned at the first sample.
 */
static esp_err_t wav_open(FILE *f, fmt_chunk_t *out_fmt, uint32_t *out_data_bytes)
{
    riff_header_t rh;
    if (fread(&rh, 1, sizeof(rh), f) != sizeof(rh) ||
        memcmp(rh.riff, "RIFF", 4) != 0 || memcmp(rh.wave, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    bool have_fmt = false;
    chunk_header_t ch;

    while (fread(&ch, 1, sizeof(ch), f) == sizeof(ch)) {
        if (memcmp(ch.id, "fmt ", 4) == 0) {
            fmt_chunk_t fmt;
            const size_t want = (ch.size < sizeof(fmt)) ? ch.size : sizeof(fmt);
            if (fread(&fmt, 1, want, f) != want) {
                return ESP_ERR_INVALID_SIZE;
            }
            /* Skip any extension bytes this chunk carries beyond fmt_chunk_t. */
            if (ch.size > want) {
                fseek(f, (long)(ch.size - want), SEEK_CUR);
            }
            *out_fmt = fmt;
            have_fmt = true;
        } else if (memcmp(ch.id, "data", 4) == 0) {
            if (!have_fmt) {
                return ESP_ERR_INVALID_STATE;
            }
            *out_data_bytes = ch.size;
            return ESP_OK;
        } else {
            /* LIST/INFO and friends. Chunks are word-aligned. */
            fseek(f, (long)(ch.size + (ch.size & 1)), SEEK_CUR);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void wav_task(void *arg)
{
    (void)arg;

    FILE *f = fopen(s_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s", s_path);
        goto out;
    }

    fmt_chunk_t fmt = {0};
    uint32_t data_bytes = 0;
    const esp_err_t ret = wav_open(f, &fmt, &data_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s is not a usable WAV file (%s)", s_path, esp_err_to_name(ret));
        goto out;
    }

    if (fmt.audio_format != 1 || fmt.bits_per_sample != 16) {
        ESP_LOGE(TAG, "%s: only 16-bit PCM is supported (format=%u bits=%u)",
                 s_path, fmt.audio_format, fmt.bits_per_sample);
        goto out;
    }
    if (fmt.sample_rate != CONFIG_BSP_AUDIO_SAMPLE_RATE) {
        ESP_LOGW(TAG, "%s is %" PRIu32 " Hz but the codec is open at %d Hz - "
                 "it will play at the wrong pitch",
                 s_path, fmt.sample_rate, CONFIG_BSP_AUDIO_SAMPLE_RATE);
    }

    s_byte_rate = fmt.byte_rate ? fmt.byte_rate
                                : fmt.sample_rate * fmt.num_channels * 2;
    s_dur_ms = s_byte_rate ? (uint32_t)(((uint64_t)data_bytes * 1000) / s_byte_rate) : 0;
    s_pos_ms = 0;

    uint8_t *buf = heap_caps_malloc(WAV_CHUNK_BYTES, MALLOC_CAP_DEFAULT);
    if (buf == NULL) {
        ESP_LOGE(TAG, "out of memory");
        goto out_close;
    }

    bsp_audio_amp_enable(true);

    uint32_t played = 0;
    while (s_run && played < data_bytes) {
        const uint32_t want = (data_bytes - played < WAV_CHUNK_BYTES)
                                  ? (data_bytes - played) : WAV_CHUNK_BYTES;
        const size_t got = fread(buf, 1, want, f);
        if (got == 0) {
            break;
        }

        size_t written = 0;
        if (bsp_audio_write(buf, got, &written) != ESP_OK) {
            ESP_LOGE(TAG, "codec write failed");
            break;
        }

        played += (uint32_t)got;
        if (s_byte_rate) {
            s_pos_ms = (uint32_t)(((uint64_t)played * 1000) / s_byte_rate);
        }
    }

    bsp_audio_amp_enable(false);
    free(buf);

out_close:
    fclose(f);
out:
    s_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t app_wav_play(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bsp_audio_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    app_wav_stop();

    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    s_pos_ms = 0;
    s_dur_ms = 0;
    s_run = true;

    /* Priority 4 keeps it below the LVGL task so audio buffering never
     * starves the UI; the I2S DMA queue absorbs the jitter. */
    if (xTaskCreate(wav_task, "wav", 4096, NULL, 4, &s_task) != pdPASS) {
        s_run = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_wav_stop(void)
{
    if (!s_run) {
        return;
    }
    s_run = false;
    for (int i = 0; i < 100 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool app_wav_is_playing(void)
{
    return s_run;
}

const char *app_wav_current(void)
{
    return s_run ? s_path : "";
}

uint32_t app_wav_position_ms(void)
{
    return s_pos_ms;
}

uint32_t app_wav_duration_ms(void)
{
    return s_dur_ms;
}
