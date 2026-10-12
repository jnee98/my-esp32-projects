#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "config.h"
#include "sensor.h"
#include "display.h"

static const char *TAG = "MAIN";

#define UART_PORT UART_NUM_0
#define BUF_SIZE 128

// State
static bool calibration_mode = false;
static float last_air_gap = -1.0f;
static float last_water_depth = -1.0f;
static bool last_calibrated = false;
static int consecutive_errors = 0;

// Timing
static int64_t last_sensor_read = 0;
static int64_t last_display_update = 0;
static int64_t last_serial_print = 0;

static void print_help(void) {
    printf("\n=== WATER LEVEL MONITOR COMMANDS ===\n");
    printf("  h / help       - Show this help\n");
    printf("  c / calibrate  - Toggle calibration mode\n");
    printf("  d <cm>         - Set datum distance (e.g., d 150.5)\n");
    printf("  r / reset      - Clear calibration\n");
    printf("  s / single     - Take single reading\n");
    printf("====================================\n\n");
}

static void handle_datum_command(const char *cmd) {
    float datum = atof(cmd + 2);
    if (datum > 0) {
        sensor_set_datum(datum);
        printf("Datum set to %.1f cm\n", datum);
    } else {
        printf("Usage: d <cm> (e.g., d 150.5)\n");
    }
}

static void handle_serial_input(void) {
    uint8_t data[BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, 0);
    if (len <= 0) return;

    data[len] = '\0';
    char *line = (char *)data;
    
    // Trim whitespace
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }
    
    // Convert to lowercase for comparison
    for (int i = 0; i < len; i++) {
        if (line[i] >= 'A' && line[i] <= 'Z') line[i] += 32;
    }

    if (strcmp(line, "h") == 0 || strcmp(line, "help") == 0) {
        print_help();
    }
    else if (strcmp(line, "c") == 0 || strcmp(line, "calibrate") == 0) {
        calibration_mode = !calibration_mode;
        if (calibration_mode) {
            printf("\n>>> CALIBRATION MODE ENABLED <<<\n");
            printf("Adjust water level, measure sensor-to-bottom distance\n");
            printf("Use 'd <cm>' to set datum, or 'c' to exit\n\n");
        } else {
            printf("\n>>> CALIBRATION MODE DISABLED <<<\n\n");
        }
    }
    else if (strncmp(line, "d ", 2) == 0) {
        handle_datum_command(line);
    }
    else if (strcmp(line, "r") == 0 || strcmp(line, "reset") == 0) {
        sensor_set_datum(0.0f);
        printf("Calibration cleared\n");
    }
    else if (strcmp(line, "s") == 0 || strcmp(line, "single") == 0) {
        printf("Taking single reading...\n");
        sensor_reading_t reading = sensor_read();
        if (reading.valid) {
            printf("Air Gap: %.1f cm\n", reading.air_gap_cm);
            if (reading.calibrated) {
                if (reading.water_depth_cm >= 0) {
                    printf("Water Depth: %.1f cm\n", reading.water_depth_cm);
                } else {
                    printf("Water Depth: OVERFLOW\n");
                }
            }
        } else {
            printf("ERROR: No valid reading\n");
        }
    }
    else if (len > 0) {
        printf("Unknown: %s (type 'h' for help)\n", line);
    }
}

static void read_sensor_task(void) {
    sensor_reading_t reading = sensor_read();
    
    if (!reading.valid) {
        consecutive_errors++;
        if (consecutive_errors >= 5) {
            display_show_status("SENSOR ERROR - CHECK WIRING", 0);
        } else {
            display_show_status("SENSOR ERROR", 3000);
        }
        return;
    }

    consecutive_errors = 0;
    last_air_gap = reading.air_gap_cm;
    last_water_depth = reading.water_depth_cm;
    last_calibrated = reading.calibrated;

    if (calibration_mode) {
        display_show_calibration(reading.air_gap_cm);
    }
}

static void update_display_task(void) {
    if (!calibration_mode) {
        display_data_t data = {
            .air_gap_cm = last_air_gap,
            .water_depth_cm = last_water_depth,
            .calibrated = last_calibrated
        };
        display_update(&data);
    }
}

static void print_serial_task(void) {
    if (!calibration_mode) {
        printf("Air Gap: ");
        if (last_air_gap >= 0) {
            printf("%.1f cm", last_air_gap);
        } else {
            printf("NO ECHO");
        }

        if (last_calibrated) {
            if (last_water_depth >= 0) {
                printf(" | Depth: %.1f cm", last_water_depth);
            } else {
                printf(" | Depth: OVERFLOW");
            }
        } else {
            printf(" | Depth: UNCALIBRATED");
        }
        printf("\n");
    }
}

void app_main(void) {
    // Initialize UART for serial CLI
    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));

    printf("\n========================================\n");
    printf("  ESP32-C6 WATER LEVEL MONITOR v1.0.0\n");
    printf("  HC-SR04 + SSD1306 OLED (ESP-IDF)\n");
    printf("========================================\n");
    printf("TRIG: GPIO%d, ECHO: GPIO%d\n", TRIG_PIN, ECHO_PIN);
    printf("OLED SDA: GPIO%d, SCL: GPIO%d\n", OLED_SDA_PIN, OLED_SCL_PIN);
    printf("Datum: %.1f cm\n", sensor_get_datum());
    printf("----------------------------------------\n");
    print_help();

    // Initialize hardware
    sensor_init();
    if (!display_init()) {
        ESP_LOGE(TAG, "Display init failed!");
    } else {
        display_show_splash();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Main loop
    while (1) {
        int64_t now = esp_timer_get_time() / 1000; // ms

        handle_serial_input();

        if (now - last_sensor_read >= SENSOR_READ_INTERVAL_MS) {
            last_sensor_read = now;
            read_sensor_task();
        }

        if (now - last_display_update >= DISPLAY_UPDATE_MS) {
            last_display_update = now;
            update_display_task();
        }

        if (now - last_serial_print >= SERIAL_PRINT_MS) {
            last_serial_print = now;
            print_serial_task();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}