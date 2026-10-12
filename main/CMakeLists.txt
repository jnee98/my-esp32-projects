idf_component_register(
    SRCS "main.c" "sensor.c" "display.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos
)