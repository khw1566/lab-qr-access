idf_component_register(
    SRCS "display.c" "ui.c" "ui_settings.c" "lab_font.c" "panel_init.c" "main.c" "board.c" "wifi.c" "camera_qr.c" "door.c" "server.c"
         "voice.c" "ui_assets.c" "ui_art.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls mbedtls
             esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_lcd esp_timer esp_hw_support
             esp_video esp_wifi_remote esp_lcd_jd9165 esp_lcd_touch_gt911 esp_codec_dev lvgl vfs quirc ov02c10
    # 안내 음성은 tools/gen_voice.py가 만든다
    EMBED_FILES "voice/scan.pcm" "voice/checking.pcm" "voice/granted.pcm" "voice/denied.pcm" "voice/offline.pcm"
                "assets/icon_0.a8" "assets/icon_1.a8" "assets/icon_2.a8"
                "assets/icon_3.a8" "assets/icon_4.a8" "assets/icon_5.a8"
                "assets/icon_6.a8" "assets/icon_7.a8" "assets/icon_8.a8"
                "assets/icon_9.a8" "assets/icon_10.a8" "assets/icon_11.a8"
                "assets/icon_12.a8"
)
