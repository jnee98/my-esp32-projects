idf_component_register(
    SRCS "main.c" "sensor.c" "display.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos esp_wifi esp_event esp_netif esp_http_server esp_websocket_server nvs_flash
)