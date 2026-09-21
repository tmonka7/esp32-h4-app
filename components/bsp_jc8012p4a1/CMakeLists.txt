idf_component_register(
    SRCS
        "src/bsp_board.c"
        "src/bsp_i2c.c"
        "src/bsp_display.c"
        "src/bsp_sdcard.c"
        "src/bsp_audio.c"
        "src/bsp_rtc.c"
        "src/bsp_power.c"
        "src/bsp_uart_test.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # "driver" is the umbrella component; it pulls in esp_driver_gpio,
        # _i2c, _i2s, _ledc, _uart and _sdmmc. Requiring it by name keeps this
        # component buildable on v5.3 through v5.5 without tracking the
        # driver split.
        driver
        esp_lcd
        esp_adc
        esp_lcd_jd9365
        esp_lcd_touch
        esp_lcd_touch_gsl3680
        esp_lvgl_port
        lvgl
        sdmmc
        fatfs
        vfs
    PRIV_REQUIRES
        esp_timer
        esp_hw_support
        esp_codec_dev
)
