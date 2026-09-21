/*
 * Battery sense and SoC temperature.
 *
 * The board carries an IP5306 power-path IC with a Li-ion connector. BAT+ goes
 * through a 68k/100k divider to GPIO52 (ADC2 channel 3), so
 *
 *     V_bat = V_adc * (68 + 100) / 100
 *
 * The IP5306 on this board is the button/LED variant with no I2C, and its LED1
 * ..LED3 outputs are not wired to the ESP32-P4, so charge state cannot be read
 * back. Only the terminal voltage is observable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include "driver/temperature_sensor.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_pwr";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;
static temperature_sensor_handle_t s_temp;

esp_err_t bsp_power_init(void)
{
    if (s_adc != NULL) {
        return ESP_OK;
    }

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BSP_BAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "adc unit");

    /* 12 dB gives roughly 0..3.1 V full scale. A full cell at 4.2 V reads
     * 2.5 V after the divider, comfortably inside that. */
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BSP_BAT_ADC_CHANNEL, &chan_cfg),
                        TAG, "adc channel");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BSP_BAT_ADC_UNIT,
        .chan     = BSP_BAT_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "no eFuse ADC calibration - battery voltage will be approximate");
    }

    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&temp_cfg, &s_temp) == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(temperature_sensor_enable(s_temp));
    } else {
        ESP_LOGW(TAG, "temperature sensor unavailable");
        s_temp = NULL;
    }

    ESP_LOGI(TAG, "battery sense on GPIO%d (ADC%d ch%d), divider %dk/%dk",
             BSP_BAT_ADC_GPIO, (int)BSP_BAT_ADC_UNIT + 1, (int)BSP_BAT_ADC_CHANNEL,
             BSP_BAT_DIV_TOP_KOHM, BSP_BAT_DIV_BOT_KOHM);
    return ESP_OK;
}

esp_err_t bsp_power_battery_mv(int *out_mv)
{
    ESP_RETURN_ON_FALSE(out_mv != NULL, ESP_ERR_INVALID_ARG, TAG, "null out");
    ESP_RETURN_ON_FALSE(s_adc != NULL, ESP_ERR_INVALID_STATE, TAG, "power not initialised");

    /* Average a handful of samples; the divider is high-impedance and the
     * reading jitters by tens of millivolts otherwise. */
    int32_t acc = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BSP_BAT_ADC_CHANNEL, &raw), TAG, "adc read");
        acc += raw;
    }
    const int raw_avg = (int)(acc / samples);

    int adc_mv = 0;
    if (s_cali_ok) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw_avg, &adc_mv), TAG, "adc cali");
    } else {
        /* 12-bit default width, ~3100 mV full scale. */
        adc_mv = (raw_avg * 3100) / 4095;
    }

    *out_mv = (adc_mv * (BSP_BAT_DIV_TOP_KOHM + BSP_BAT_DIV_BOT_KOHM)) / BSP_BAT_DIV_BOT_KOHM;
    return ESP_OK;
}

int bsp_power_battery_percent(int mv)
{
    /* Resting-voltage breakpoints for a single Li-ion cell. Under load the
     * curve shifts down, so treat this as indicative, not a fuel gauge. */
    static const struct {
        int mv;
        int pct;
    } curve[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3850, 60},
        {3800, 50},  {3750, 40}, {3700, 30}, {3650, 20}, {3550, 10},
        {3400, 5},   {3200, 0},
    };

    if (mv <= 0 || mv > 4600) {
        return -1;  /* nothing plugged in, or not a single Li-ion cell */
    }
    if (mv >= curve[0].mv) {
        return 100;
    }
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) {
            const int span_mv  = curve[i - 1].mv - curve[i].mv;
            const int span_pct = curve[i - 1].pct - curve[i].pct;
            return curve[i].pct + ((mv - curve[i].mv) * span_pct) / span_mv;
        }
    }
    return 0;
}

esp_err_t bsp_power_temperature(float *out_celsius)
{
    ESP_RETURN_ON_FALSE(out_celsius != NULL, ESP_ERR_INVALID_ARG, TAG, "null out");
    ESP_RETURN_ON_FALSE(s_temp != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "no temperature sensor");
    return temperature_sensor_get_celsius(s_temp, out_celsius);
}
