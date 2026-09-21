/*
 * Minimal 16-bit PCM RIFF/WAVE player on top of the BSP audio path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start playing a .wav file. Any current playback is stopped first.
 *  Returns ESP_ERR_NOT_SUPPORTED for formats other than 16-bit PCM. */
esp_err_t app_wav_play(const char *path);

/** Stop playback and wait for the worker to finish. */
void app_wav_stop(void);

bool app_wav_is_playing(void);

/** File currently playing, or "" when idle. Points at internal storage. */
const char *app_wav_current(void);

uint32_t app_wav_position_ms(void);
uint32_t app_wav_duration_ms(void);

#ifdef __cplusplus
}
#endif
