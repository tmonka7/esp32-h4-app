/*
 * Shared I2C0 bus: GSL3680 touch (0x40), ES8311 codec (0x18), RX8025T RTC
 * (0x32) and the camera/expansion headers all hang off SDA=GPIO7, SCL=GPIO8.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "bsp_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }


    const i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* The board fits 5k1 pull-ups on both lines (R53/R54), so the weak
         * internal ones are not needed and would only slow the edges. */
        .flags.enable_internal_pullup = false,
    };

    esp_err_t ret = i2c_new_master_bus(&cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C%d up on SDA=%d SCL=%d @ %d Hz",
             BSP_I2C_PORT, BSP_I2C_SDA, BSP_I2C_SCL, BSP_I2C_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    return s_bus;
}

bool bsp_i2c_probe(uint8_t addr_7bit)
{
    if (s_bus == NULL) {
        return false;
    }
    /* 50 ms is generous; the RTC in particular can stretch the clock after a
     * cold start while its oscillator settles. */
    return i2c_master_probe(s_bus, addr_7bit, 50) == ESP_OK;
}
