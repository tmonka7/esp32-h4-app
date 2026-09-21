/*
 * Audio output: ES8311 codec on I2C0 (8-bit address 0x30 / 7-bit 0x18) driving
 * an NS4150 class-D amplifier. PA_CTRL is GPIO20, active high.
 *
 * I2S0 in standard Philips mode, master, with MCLK: MCLK GPIO13, BCLK GPIO12,
 * WS GPIO10, DOUT GPIO9 (to the codec), DIN GPIO11 (from the ES7210 path).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "bsp_audio";

static i2s_chan_handle_t      s_tx_chan;
static i2s_chan_handle_t      s_rx_chan;
static esp_codec_dev_handle_t s_codec;
static uint32_t               s_sample_rate;
static int                    s_volume = CONFIG_BSP_AUDIO_VOLUME_DEFAULT;

bool bsp_audio_is_ready(void)
{
    return s_codec != NULL;
}

static esp_err_t i2s_init(uint32_t sample_rate_hz)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "i2s_new_channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_BCLK,
            .ws   = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s tx std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &std_cfg), TAG, "i2s rx std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "i2s rx enable");
    return ESP_OK;
}

esp_err_t bsp_audio_init(uint32_t sample_rate_hz)
{
    if (s_codec != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");

    if (!bsp_i2c_probe(BSP_ES8311_I2C_ADDR)) {
        ESP_LOGW(TAG, "no ES8311 at 0x%02X - audio disabled", BSP_ES8311_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(i2s_init(sample_rate_hz), TAG, "i2s");
    s_sample_rate = sample_rate_hz;

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = s_tx_chan,
        .rx_handle = s_rx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "codec i2s data interface");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = BSP_I2C_PORT,
        /* esp_codec_dev takes the 8-bit form and shifts it down itself. */
        .addr       = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_FAIL, TAG, "codec i2c ctrl interface");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_FAIL, TAG, "codec gpio interface");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if     = ctrl_if,
        .gpio_if     = gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,   /* the ESP32-P4 is I2S master */
        .use_mclk    = true,
        .digital_mic = false,
        .hw_gain = {
            .pa_voltage        = 5.0f,  /* NS4150 runs from VOUT-BAT (5 V) */
            .codec_dac_voltage = 3.3f,
            .pa_gain           = 0.0f,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_FAIL, TAG, "es8311_codec_new");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_FAIL, TAG, "esp_codec_dev_new");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0,
        .sample_rate     = sample_rate_hz,
        .mclk_multiple   = 0,   /* 256 x Fs */
    };
    const int rc = esp_codec_dev_open(s_codec, &fs);
    if (rc != 0) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed (%d)", rc);
        s_codec = NULL;
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(s_codec, s_volume);
    ESP_LOGI(TAG, "ES8311 up at %" PRIu32 " Hz, volume %d%%", sample_rate_hz, s_volume);
    return ESP_OK;
}

esp_err_t bsp_audio_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    s_volume = percent;

    if (s_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return (esp_codec_dev_set_out_vol(s_codec, percent) == 0) ? ESP_OK : ESP_FAIL;
}

int bsp_audio_get_volume(void)
{
    return s_volume;
}

esp_err_t bsp_audio_amp_enable(bool enable)
{
    if (s_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The ES8311 driver owns PA_CTRL; muting the output drops the amplifier
     * with it, which is what we want and avoids fighting over the pin. */
    return (esp_codec_dev_set_out_mute(s_codec, !enable) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_write(const void *pcm, size_t bytes, size_t *bytes_written)
{
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_ERR_INVALID_STATE, TAG, "audio not initialised");

    const int rc = esp_codec_dev_write(s_codec, (void *)pcm, (int)bytes);
    if (bytes_written != NULL) {
        *bytes_written = (rc == 0) ? bytes : 0;
    }
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_play_tone(uint32_t freq_hz, uint32_t ms)
{
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_ERR_INVALID_STATE, TAG, "audio not initialised");
    ESP_RETURN_ON_FALSE(freq_hz > 0 && ms > 0, ESP_ERR_INVALID_ARG, TAG, "bad tone");

    /* Generate and push 50 ms at a time so a long tone does not need a big
     * buffer and the caller can still be pre-empted. */
    const uint32_t chunk_frames = s_sample_rate / 20;
    const size_t   chunk_bytes  = chunk_frames * 2 * sizeof(int16_t);

    int16_t *buf = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the tone buffer");

    const uint32_t total_frames = (s_sample_rate * ms) / 1000;
    uint32_t       done_frames  = 0;
    uint32_t       phase        = 0;
    esp_err_t      ret          = ESP_OK;

    esp_codec_dev_set_out_mute(s_codec, false);

    while (done_frames < total_frames) {
        const uint32_t n = (total_frames - done_frames < chunk_frames)
                               ? (total_frames - done_frames) : chunk_frames;
        for (uint32_t i = 0; i < n; i++) {
            const float t = (float)(phase + i) / (float)s_sample_rate;
            const int16_t s = (int16_t)(8000.0f * sinf(2.0f * (float)M_PI * (float)freq_hz * t));
            buf[2 * i]     = s;   /* left  */
            buf[2 * i + 1] = s;   /* right */
        }
        phase += n;

        if (esp_codec_dev_write(s_codec, buf, (int)(n * 2 * sizeof(int16_t))) != 0) {
            ret = ESP_FAIL;
            break;
        }
        done_frames += n;
    }

    free(buf);
    return ret;
}
