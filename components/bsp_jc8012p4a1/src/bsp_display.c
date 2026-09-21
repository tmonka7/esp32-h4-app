/*
 * Display + touch bring-up for the JC8012P4A1.
 *
 *   DSI PHY power : on-chip LDO channel 3 @ 2.5 V
 *   DSI bus       : 2 data lanes, 1500 Mbps/lane
 *   Panel         : JD9365, 800x1280, DPI clock 60 MHz, RGB565
 *   Touch         : GSL3680 on I2C0 @ 0x40, RST GPIO22, INT GPIO21
 *   Backlight     : LEDC PWM on GPIO23, 20 kHz (above the audible range)
 *
 * The panel is portrait. The UI is landscape, so LVGL rotates in software;
 * see CONFIG_BSP_DISPLAY_ROTATION_*.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/jc8012p4a1.h"

#include <inttypes.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_gsl3680.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "bsp_disp";

#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ     (20000)
#define BL_DUTY_MAX         ((1 << 10) - 1)

static esp_lcd_panel_handle_t   s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_dsi_bus_handle_t s_dsi_bus;
static esp_lcd_touch_handle_t   s_touch;
static lv_display_t            *s_disp;
static int                      s_brightness = -1;

/* ------------------------------------------------------------------ */
/* Backlight                                                          */
/* ------------------------------------------------------------------ */

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RESOLUTION,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    const ledc_channel_config_t ch = {
        .gpio_num   = BSP_LCD_BACKLIGHT,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");

    s_brightness = 0;
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int percent)
{
    ESP_RETURN_ON_FALSE(s_brightness >= 0, ESP_ERR_INVALID_STATE, TAG, "backlight not initialised");

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    const uint32_t duty = (uint32_t)((BL_DUTY_MAX * percent) / 100);
    ESP_RETURN_ON_ERROR(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty), TAG, "ledc set duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL), TAG, "ledc update duty");

    s_brightness = percent;
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    return s_brightness < 0 ? 0 : s_brightness;
}

/* ------------------------------------------------------------------ */
/* Panel                                                              */
/* ------------------------------------------------------------------ */

static esp_err_t dsi_phy_power_on(void)
{
    static esp_ldo_channel_handle_t phy_ldo;
    if (phy_ldo != NULL) {
        return ESP_OK;
    }

    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = BSP_LCD_DSI_PHY_LDO_CHAN,
        .voltage_mv = BSP_LCD_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo), TAG,
                        "cannot acquire LDO ch%d for the DSI PHY", BSP_LCD_DSI_PHY_LDO_CHAN);
    ESP_LOGI(TAG, "DSI PHY powered from LDO ch%d @ %d mV",
             BSP_LCD_DSI_PHY_LDO_CHAN, BSP_LCD_DSI_PHY_LDO_MV);
    return ESP_OK;
}

static esp_err_t panel_init(void)
{
    ESP_RETURN_ON_ERROR(dsi_phy_power_on(), TAG, "DSI PHY power");

    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id             = 0,
        .num_data_lanes     = BSP_LCD_DSI_LANE_NUM,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_LCD_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &s_dsi_bus), TAG, "new DSI bus");

    /* Commands and parameters go over DBI, pixels over DPI. */
    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_panel_io), TAG, "new DBI io");

    /* Written out rather than using JD9365_800_1280_PANEL_60HZ_DPI_CONFIG()
     * so that only fields present in every supported ESP-IDF are named. In
     * particular num_fbs is left at 0, which the driver documents as "one
     * frame buffer" - the same thing the vendor macro asks for explicitly,
     * but without depending on the field existing. Timings otherwise match
     * the vendor configuration for this panel. */
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_LCD_DPI_CLK_MHZ,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size            = BSP_LCD_H_RES,
            .v_size            = BSP_LCD_V_RES,
            .hsync_pulse_width = 20,
            .hsync_back_porch  = 20,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 20,
        },
        .flags.use_dma2d = true,
    };

    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num   = BSP_LCD_DSI_LANE_NUM,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(s_panel_io, &panel_config, &s_panel), TAG, "new JD9365");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel on");

    ESP_LOGI(TAG, "JD9365 %dx%d up (%d lanes @ %d Mbps, DPI %d MHz)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_DSI_LANE_NUM,
             BSP_LCD_DSI_LANE_MBPS, BSP_LCD_DPI_CLK_MHZ);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Touch                                                              */
/* ------------------------------------------------------------------ */

/* Kconfig booleans are simply absent when unset, so they cannot be used
 * directly inside a C expression such as a struct initialiser. */
#ifdef CONFIG_BSP_TOUCH_SWAP_XY
#define TOUCH_SWAP_XY  1
#else
#define TOUCH_SWAP_XY  0
#endif
#ifdef CONFIG_BSP_TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 1
#else
#define TOUCH_MIRROR_X 0
#endif
#ifdef CONFIG_BSP_TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 1
#else
#define TOUCH_MIRROR_Y 0
#endif

static esp_err_t touch_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,
        .y_max        = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_RST,
        .int_gpio_num = BSP_TOUCH_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
    };

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    tp_io_config.scl_speed_hz = BSP_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &tp_io_config, &tp_io),
                        TAG, "touch panel io");

    /* The GSL3680 needs its firmware downloaded over I2C on every power-up,
     * which the driver does inside this call. It takes a few hundred ms. */
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gsl3680(tp_io, &tp_cfg, &s_touch), TAG, "GSL3680");

    ESP_LOGI(TAG, "GSL3680 touch up (RST=%d INT=%d)", BSP_TOUCH_RST, BSP_TOUCH_INT);
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return s_touch;
}

/* ------------------------------------------------------------------ */
/* LVGL                                                               */
/* ------------------------------------------------------------------ */

static lv_display_rotation_t configured_rotation(void)
{
#if defined(CONFIG_BSP_DISPLAY_ROTATION_90)
    return LV_DISPLAY_ROTATION_90;
#elif defined(CONFIG_BSP_DISPLAY_ROTATION_180)
    return LV_DISPLAY_ROTATION_180;
#elif defined(CONFIG_BSP_DISPLAY_ROTATION_270)
    return LV_DISPLAY_ROTATION_270;
#else
    return LV_DISPLAY_ROTATION_0;
#endif
}

lv_display_t *bsp_display_start(void)
{
    if (s_disp != NULL) {
        return s_disp;
    }

    if (backlight_init() != ESP_OK) {
        ESP_LOGE(TAG, "backlight init failed");
        return NULL;
    }
    if (panel_init() != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return NULL;
    }

    const lvgl_port_cfg_t port_cfg = {
        .task_priority    = CONFIG_BSP_DISPLAY_LVGL_TASK_PRIORITY,
        .task_stack       = CONFIG_BSP_DISPLAY_LVGL_TASK_STACK,
        .task_affinity    = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms  = 5,
    };
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return NULL;
    }

    const lv_display_rotation_t rotation = configured_rotation();
    const bool needs_sw_rotate = (rotation != LV_DISPLAY_ROTATION_0);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_panel_io,
        .panel_handle = s_panel,
        .buffer_size  = BSP_LCD_H_RES * CONFIG_BSP_DISPLAY_LVGL_BUF_LINES,
        .double_buffer = true,
        /* Physical panel geometry. LVGL swaps these itself once rotated. */
        .hres         = BSP_LCD_H_RES,
        .vres         = BSP_LCD_V_RES,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,
            .sw_rotate   = needs_sw_rotate,
            .swap_bytes  = false,
            .full_refresh = false,
            .direct_mode = false,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags.avoid_tearing = false,   /* incompatible with software rotation */
    };

    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return NULL;
    }

    if (needs_sw_rotate) {
        lvgl_port_lock(0);
        lv_display_set_rotation(s_disp, rotation);
        lvgl_port_unlock();
    }

    if (touch_init() == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = s_disp,
            .handle = s_touch,
        };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGE(TAG, "lvgl_port_add_touch failed - display will be read-only");
        }
    } else {
        ESP_LOGE(TAG, "touch init failed - display will be read-only");
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(CONFIG_BSP_DISPLAY_BRIGHTNESS_DEFAULT));

    ESP_LOGI(TAG, "LVGL up: %" PRId32 "x%" PRId32 " (rotation %d)",
             lv_display_get_horizontal_resolution(s_disp),
             lv_display_get_vertical_resolution(s_disp),
             (int)rotation);
    return s_disp;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
