idf_component_register(
    SRCS "main.c" "board.c" "wifi.c" "camera_qr.c" "door.c" "server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls
             esp_driver_gpio esp_driver_i2c esp_timer
             esp_video esp_wifi_remote vfs quirc
)
