/*
 * JC8012P4A1_BSP_ESP32P4 - Smart Control Panel
 *
 * Boot order:
 *   1. Board bring-up (I2C, power sense, RTC, display, touch, LVGL, UART,
 *      TF card, audio codec), then Wi-Fi through the ESP32-C6.
 *   2. Report what is present on the console, so a headless board still tells
 *      you what worked.
 *   3. Build the UI under the LVGL lock and hand over to the LVGL task.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include <stdio.h>

#include "app_wifi.h"
#include "bsp/bsp_uart_test.h"
#include "bsp/jc8012p4a1.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui/ui.h"

static const char *TAG = "app";

static void log_boot_banner(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, " JC8012P4A1_BSP_ESP32P4  -  %s", app->version);
    ESP_LOGI(TAG, " built %s %s with ESP-IDF %s", app->date, app->time, app->idf_ver);
    ESP_LOGI(TAG, " %s rev v%d.%d, %d core(s)",
             CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG, " internal heap %u B, PSRAM %u B free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "================================================");
}

static void log_peripheral_summary(esp_err_t wifi_err)
{
    ESP_LOGI(TAG, "peripheral summary:");
    ESP_LOGI(TAG, "  display   JD9365 800x1280 MIPI-DSI  : up");
    ESP_LOGI(TAG, "  touch     GSL3680 @ 0x40            : %s",
             bsp_touch_get_handle() != NULL ? "up" : "MISSING");
    ESP_LOGI(TAG, "  rtc       RX8025T @ 0x32            : %s",
             bsp_rtc_present() ? "up" : "not detected");
    ESP_LOGI(TAG, "  audio     ES8311 @ 0x18 + NS4150    : %s",
             bsp_audio_is_ready() ? "up" : "not detected");
    ESP_LOGI(TAG, "  tf card   SDMMC slot 0              : %s",
             bsp_sdcard_is_mounted() ? "mounted at " BSP_SD_MOUNT_POINT : "not mounted");
    ESP_LOGI(TAG, "  wifi      ESP32-C6 over SDIO slot 1  : %s",
             wifi_err == ESP_OK ? "SoftAP \"" CONFIG_APP_WIFI_SSID "\" starting"
             : wifi_err == ESP_ERR_INVALID_STATE ? "off (no SSID configured)"
             : wifi_err == ESP_ERR_INVALID_ARG ? "off (AP password too short)"
             : esp_err_to_name(wifi_err));
    ESP_LOGI(TAG, "  console   UART0 GPIO%d TX / GPIO%d RX : up (you are reading it)",
             BSP_UART0_TX, BSP_UART0_RX);
    ESP_LOGI(TAG, "  test uart UART%d GPIO%d TX / GPIO%d RX : installed",
             CONFIG_BSP_UART_TEST_PORT, CONFIG_BSP_UART_TEST_TX_GPIO,
             CONFIG_BSP_UART_TEST_RX_GPIO);
}

void app_main(void)
{
    /* NVS is not used by the UI yet, but initialising it here means any
     * component that starts storing settings later just works. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);

    log_boot_banner();

    ESP_ERROR_CHECK(bsp_board_init());

    /* Non-fatal: the UI works without a network and says so where it matters. */
    const esp_err_t wifi_err = app_wifi_start();

    log_peripheral_summary(wifi_err);

    /* Everything touching lv_* has to hold the port lock, including the
     * initial build, because the LVGL task is already running by now. */
    if (bsp_display_lock(0)) {
        ui_start();
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "could not take the LVGL lock; UI not started");
    }

    ESP_LOGI(TAG, "running");
}
