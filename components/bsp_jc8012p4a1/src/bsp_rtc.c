/*
 * RX8025T real-time clock on I2C0 @ 0x32, backed by the CR1220 cell (BT1).
 *
 * Register map (RX8025T):
 *   0x00 SEC  0x01 MIN  0x02 HOUR  0x03 WEEK  0x04 DAY  0x05 MONTH  0x06 YEAR
 *   0x0E CONTROL   0x0F FLAG
 * Time registers are BCD. WEEK is a one-hot bitmap, not a number.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_rtc";

#define RX8025T_REG_SEC      (0x00)
#define RX8025T_REG_CONTROL  (0x0E)
#define RX8025T_REG_FLAG     (0x0F)
#define RX8025T_FLAG_VLF     (1 << 1)   /* voltage low: contents are invalid */

static i2c_master_dev_handle_t s_dev;
static bool s_present;

static inline uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static inline uint8_t bin_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "rtc not initialised");
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "rtc not initialised");
    ESP_RETURN_ON_FALSE(len <= 8, ESP_ERR_INVALID_ARG, TAG, "write too long");

    uint8_t tmp[9];
    tmp[0] = reg;
    memcpy(&tmp[1], buf, len);
    return i2c_master_transmit(s_dev, tmp, len + 1, 100);
}

esp_err_t bsp_rtc_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");

    if (!bsp_i2c_probe(BSP_RX8025T_I2C_ADDR)) {
        ESP_LOGW(TAG, "no RX8025T at 0x%02X - falling back to the system clock only",
                 BSP_RX8025T_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_RX8025T_I2C_ADDR,
        .scl_speed_hz    = BSP_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev_cfg, &s_dev),
                        TAG, "add RTC device");

    uint8_t flag = 0;
    if (rtc_read(RX8025T_REG_FLAG, &flag, 1) == ESP_OK && (flag & RX8025T_FLAG_VLF)) {
        ESP_LOGW(TAG, "VLF set: the backup cell was flat, RTC time is not trustworthy");
    }

    s_present = true;
    ESP_LOGI(TAG, "RX8025T found at 0x%02X", BSP_RX8025T_I2C_ADDR);
    return ESP_OK;
}

bool bsp_rtc_present(void)
{
    return s_present;
}

esp_err_t bsp_rtc_get_time(struct tm *out_tm)
{
    ESP_RETURN_ON_FALSE(out_tm != NULL, ESP_ERR_INVALID_ARG, TAG, "null tm");
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_INVALID_STATE, TAG, "no RTC");

    uint8_t r[7] = {0};
    ESP_RETURN_ON_ERROR(rtc_read(RX8025T_REG_SEC, r, sizeof(r)), TAG, "read time");

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_sec  = bcd_to_bin(r[0] & 0x7F);
    out_tm->tm_min  = bcd_to_bin(r[1] & 0x7F);
    out_tm->tm_hour = bcd_to_bin(r[2] & 0x3F);
    out_tm->tm_mday = bcd_to_bin(r[4] & 0x3F);
    out_tm->tm_mon  = bcd_to_bin(r[5] & 0x1F) - 1;      /* tm_mon is 0-based */
    out_tm->tm_year = bcd_to_bin(r[6]) + 100;           /* tm_year is from 1900, RTC from 2000 */
    out_tm->tm_isdst = -1;

    /* r[3] is a one-hot day-of-week bitmap. Let mktime derive tm_wday from the
     * date instead of trusting whatever the register holds. */
    const time_t t = mktime(out_tm);
    if (t != (time_t)-1) {
        struct tm norm;
        if (localtime_r(&t, &norm) != NULL) {
            *out_tm = norm;
        }
    }
    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(const struct tm *in_tm)
{
    ESP_RETURN_ON_FALSE(in_tm != NULL, ESP_ERR_INVALID_ARG, TAG, "null tm");
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_INVALID_STATE, TAG, "no RTC");
    ESP_RETURN_ON_FALSE(in_tm->tm_year >= 100 && in_tm->tm_year < 200, ESP_ERR_INVALID_ARG,
                        TAG, "RX8025T only covers 2000-2099");

    const uint8_t r[7] = {
        bin_to_bcd((uint8_t)in_tm->tm_sec),
        bin_to_bcd((uint8_t)in_tm->tm_min),
        bin_to_bcd((uint8_t)in_tm->tm_hour),
        (uint8_t)(1 << (in_tm->tm_wday & 0x07)),   /* one-hot weekday */
        bin_to_bcd((uint8_t)in_tm->tm_mday),
        bin_to_bcd((uint8_t)(in_tm->tm_mon + 1)),
        bin_to_bcd((uint8_t)(in_tm->tm_year - 100)),
    };
    ESP_RETURN_ON_ERROR(rtc_write(RX8025T_REG_SEC, r, sizeof(r)), TAG, "write time");

    /* Writing the time does not clear VLF by itself; do it so the next boot
     * does not keep warning about a stale battery. */
    uint8_t flag = 0;
    if (rtc_read(RX8025T_REG_FLAG, &flag, 1) == ESP_OK && (flag & RX8025T_FLAG_VLF)) {
        flag &= (uint8_t)~RX8025T_FLAG_VLF;
        rtc_write(RX8025T_REG_FLAG, &flag, 1);
    }
    return ESP_OK;
}

esp_err_t bsp_rtc_sync_to_system(void)
{
    struct tm t;
    ESP_RETURN_ON_ERROR(bsp_rtc_get_time(&t), TAG, "read RTC");

    const time_t epoch = mktime(&t);
    ESP_RETURN_ON_FALSE(epoch != (time_t)-1, ESP_FAIL, TAG, "RTC holds an invalid date");

    const struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    ESP_RETURN_ON_FALSE(settimeofday(&tv, NULL) == 0, ESP_FAIL, TAG, "settimeofday");

    ESP_LOGI(TAG, "system clock set from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return ESP_OK;
}
