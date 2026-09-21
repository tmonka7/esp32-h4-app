/*
 * Board bring-up order for the JC8012P4A1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/bsp_uart_test.h"
#include "bsp/jc8012p4a1.h"

#include "esp_log.h"

static const char *TAG = "bsp";

esp_err_t bsp_board_init(void)
{
    /* I2C first: touch, codec and RTC all sit on it. */
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* Log what actually answered, so a dead device shows up here rather than
     * as a confusing failure three screens later. */
    ESP_LOGI(TAG, "I2C scan: touch(0x40)=%s codec(0x18)=%s rtc(0x32)=%s",
             bsp_i2c_probe(0x40) ? "yes" : "no",
             bsp_i2c_probe(BSP_ES8311_I2C_ADDR) ? "yes" : "no",
             bsp_i2c_probe(BSP_RX8025T_I2C_ADDR) ? "yes" : "no");

    /* Battery divider on GPIO52 and the SoC temperature sensor. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_power_init());

    /* The RTC is optional; if it is missing or flat the UI just shows the
     * uninitialised system clock. */
    if (bsp_rtc_init() == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_rtc_sync_to_system());
    }

    /* Display + touch + LVGL. Without this there is no UI at all, so a
     * failure here is fatal. */
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "display bring-up failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_uart_test_init());

#if CONFIG_BSP_UART_TEST_ON_BOOT
    {
        bsp_uart_test_result_t res;
        bsp_uart_test_run(BSP_UART_TEST_INTERNAL, &res);
        bsp_uart_test_log(&res);
    }
#endif

#if CONFIG_BSP_SDCARD_MOUNT_ON_BOOT
    /* A missing or unformatted card is normal, not an error: the SD Card and
     * File Manager screens report it. bsp_sdcard_mount() already logs why. */
    (void)bsp_sdcard_mount();
#endif

#if CONFIG_BSP_AUDIO_ENABLE
    if (bsp_audio_init(CONFIG_BSP_AUDIO_SAMPLE_RATE) != ESP_OK) {
        ESP_LOGW(TAG, "audio unavailable - the Music screen will say so");
    }
#endif

    ESP_LOGI(TAG, "board init done");
    return ESP_OK;
}
