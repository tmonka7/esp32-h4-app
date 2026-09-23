idf_component_register(
    SRCS
        "main.c"
        "app_wav.c"
        "app_wifi.c"
        "cam_stream.c"
        "ui/ui_theme.c"
        "ui/ui_shell.c"
        "ui/ui_scr_home.c"
        "ui/ui_scr_system.c"
        "ui/ui_scr_storage.c"
        "ui/ui_scr_media.c"
        "ui/ui_scr_tools.c"
        "ui/ui_scr_camview.c"
    INCLUDE_DIRS
        "."
        "ui"
    REQUIRES
        bsp_jc8012p4a1
        lvgl
        nvs_flash
        esp_psram
        esp_timer
        esp_hw_support   # esp_chip_info.h, esp_mac.h
        app_update       # esp_app_get_description()
        spi_flash        # esp_flash_get_size()
        vfs
        fatfs
        esp_http_client  # Cam Remote: MJPEG over HTTP
        esp_netif
        esp_event
        esp_wifi         # API only; esp_wifi_remote forwards it to the C6
        esp_wifi_remote
        esp_hosted
)
