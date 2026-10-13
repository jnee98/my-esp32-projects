#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_websocket_server.h"
#include "nvs_flash.h"
#include "config.h"
#include "sensor.h"
#include "display.h"

static const char *TAG = "MAIN";

#define UART_PORT UART_NUM_0
#define BUF_SIZE 128

// WiFi
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

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
static int64_t last_real_data_send = 0;

// WebSocket
static httpd_handle_t server = NULL;
static int ws_client_fd = -1;

// Forward declarations
static void wifi_init_sta(void);
static esp_err_t start_webserver(void);
static void stop_webserver(void);
static esp_err_t ws_handler(httpd_req_t *req);
static esp_err_t realtime_post_handler(httpd_req_t *req);
static esp_err_t realtime_get_handler(httpd_req_t *req);
static esp_err_t realtime_stop_handler(httpd_req_t *req);
static esp_err_t tank_height_handler(httpd_req_t *req);
static esp_err_t real_data_buffer_handler(httpd_req_t *req);

static void print_help(void) {
    printf("\n=== WATER LEVEL MONITOR COMMANDS ===\n");
    printf("  h / help           - Show this help\n");
    printf("  c / calibrate      - Toggle calibration mode\n");
    printf("  d <cm>             - Set datum distance (e.g., d 150.5)\n");
    printf("  r / reset          - Clear calibration\n");
    printf("  s / single         - Take single reading\n");
    printf("  m <mode>           - Set mode (0=Live, 1=Sine, 2=Square, 3=Triangle, 4=Sawtooth, 5=Real Data)\n");
    printf("  load_real_data     - Load default sine wave into real data buffer\n");
    printf("  stop_real_data     - Stop real data playback\n");
    printf("  real_data <idx> <val> - Set real data point (e.g., real_data 0 100.5)\n");
    printf("  dump_real_data     - Dump real data buffer\n");
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

static void handle_mode_command(const char *cmd) {
    int mode = atoi(cmd + 2);
    if (mode >= 0 && mode <= 5) {
        sensor_set_mode((operation_mode_t)mode);
        const char *mode_names[] = {"Live Sensor", "Sine Wave", "Square Wave", "Triangle Wave", "Sawtooth Wave", "Real Data"};
        printf("Mode set to %d: %s\n", mode, mode_names[mode]);
    } else {
        printf("Usage: m <0-5> (0=Live, 1=Sine, 2=Square, 3=Triangle, 4=Sawtooth, 5=Real Data)\n");
    }
}

static void handle_load_real_data(void) {
    sensor_real_data_init();
    sensor_set_mode(MODE_REAL_DATA);
    printf("Real data buffer loaded with sine wave (%d samples)\n", REAL_DATA_BUFFER_SIZE);
}

static void handle_stop_real_data(void) {
    sensor_real_data_deinit();
    sensor_set_mode(MODE_LIVE_SENSOR);
    printf("Real data stopped, switched to live sensor mode\n");
}

static void handle_real_data_command(const char *cmd) {
    int idx;
    float val;
    if (sscanf(cmd + 10, "%d %f", &idx, &val) == 2) {
        sensor_real_data_set_value(idx, val);
        printf("Real data[%d] = %.1f cm\n", idx, val);
    } else {
        printf("Usage: real_data <index> <value_cm>\n");
    }
}

static void handle_dump_real_data(void) {
    int size = sensor_real_data_get_buffer_size();
    printf("Real data buffer (%d samples):\n", size);
    for (int i = 0; i < size; i++) {
        float v = sensor_real_data_get_value(i);
        if (v >= 0) {
            printf("  [%d] = %.1f cm\n", i, v);
        }
        if (i > 20) {
            printf("  ... (%d more)\n", size - i - 1);
            break;
        }
    }
}

// Update handle_serial_input to include new commands
static void handle_serial_input(void) {
    uint8_t data[BUF_SIZE];
    int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, 0);
    if (len <= 0) return;

    data[len] = '\0';
    char *line = (char *)data;

    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

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
    else if (strncmp(line, "m ", 2) == 0) {
        handle_mode_command(line);
    }
    else if (strcmp(line, "load_real_data") == 0) {
        handle_load_real_data();
    }
    else if (strcmp(line, "stop_real_data") == 0) {
        handle_stop_real_data();
    }
    else if (strncmp(line, "real_data ", 10) == 0) {
        handle_real_data_command(line);
    }
    else if (strcmp(line, "dump_real_data") == 0) {
        handle_dump_real_data();
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


// ============================================================
// WiFi Event Handler
// ============================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry WiFi connection (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done. Connecting to %s...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
    }
}

// ============================================================
// WebSocket Handler
// ============================================================
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ws_client_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket client connected: fd=%d", ws_client_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.len > 0) {
        char *buf = malloc(ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = (uint8_t *)buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
        buf[ws_pkt.len] = '\0';

        ESP_LOGI(TAG, "WS received: %s", buf);

        if (strstr(buf, "load_real_data")) {
            sensor_real_data_init();
            sensor_set_mode(MODE_REAL_DATA);
            const char *resp = "{\"status\":\"ok\",\"cmd\":\"load_real_data\",\"msg\":\"Real data loaded\"}";
            ws_pkt.payload = (uint8_t *)resp;
            ws_pkt.len = strlen(resp);
            httpd_ws_send_frame(req, &ws_pkt);
        } else if (strstr(buf, "stop_real_data")) {
            sensor_real_data_deinit();
            sensor_set_mode(MODE_LIVE_SENSOR);
            const char *resp = "{\"status\":\"ok\",\"cmd\":\"stop_real_data\",\"msg\":\"Real data stopped\"}";
            ws_pkt.payload = (uint8_t *)resp;
            ws_pkt.len = strlen(resp);
            httpd_ws_send_frame(req, &ws_pkt);
        } else {
            const char *resp = "{\"status\":\"error\",\"msg\":\"Unknown command\"}";
            ws_pkt.payload = (uint8_t *)resp;
            ws_pkt.len = strlen(resp);
            httpd_ws_send_frame(req, &ws_pkt);
        }
        free(buf);
    }
    return ESP_OK;
}

// ============================================================
// HTTP Handlers
// ============================================================
static esp_err_t realtime_post_handler(httpd_req_t *req) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char *data_start = strstr(buf, "[");
    if (data_start) {
        data_start++;
        int idx = 0;
        char *token = strtok(data_start, ",");
        while (token && idx < REAL_DATA_BUFFER_SIZE) {
            float val = atof(token);
            sensor_real_data_set_value(idx, val);
            token = strtok(NULL, ",");
            idx++;
        }
        if (idx > 0) {
            sensor_set_mode(MODE_REAL_DATA);
        }
    }

    const char *resp = "{\"status\":\"ok\",\"received\":0}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t realtime_get_handler(httpd_req_t *req) {
    sensor_reading_t reading = sensor_read();
    char json[512];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"air_gap_cm\":%.1f,"
        "\"water_depth_cm\":%.1f,"
        "\"valid\":%s,"
        "\"calibrated\":%s,"
        "\"mode\":%d,"
        "\"tank_height_cm\":%.1f,"
        "\"timestamp\":%lld"
        "}",
        reading.air_gap_cm,
        reading.water_depth_cm,
        reading.valid ? "true" : "false",
        reading.calibrated ? "true" : "false",
        sensor_get_mode(),
        TANK_HEIGHT_CM,
        esp_timer_get_time() / 1000
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static esp_err_t realtime_stop_handler(httpd_req_t *req) {
    sensor_real_data_deinit();
    sensor_set_mode(MODE_LIVE_SENSOR);
    const char *resp = "{\"status\":\"ok\",\"msg\":\"Real data stopped\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t tank_height_handler(httpd_req_t *req) {
    char json[256];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"tank_height_ft\":%.1f,"
        "\"tank_height_cm\":%.1f"
        "}",
        TANK_HEIGHT_FT, TANK_HEIGHT_CM
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static esp_err_t real_data_buffer_handler(httpd_req_t *req) {
    int size = sensor_real_data_get_buffer_size();
    char *json = malloc(size * 8 + 64);
    if (!json) return ESP_ERR_NO_MEM;

    char *p = json;
    p += sprintf(p, "{\"buffer_size\":%d,\"data\":[", size);
    for (int i = 0; i < size; i++) {
        float v = sensor_real_data_get_value(i);
        if (i > 0) p += sprintf(p, ",");
        p += sprintf(p, "%.1f", v);
    }
    p += sprintf(p, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, p - json);
    free(json);
    return ESP_OK;
}

// ============================================================
// Start/Stop Webserver
// ============================================================
static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    httpd_uri_t ws_uri = {
        .uri = WEBSOCKET_URI,
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };

    httpd_uri_t realtime_post_uri = {
        .uri = "/realtime",
        .method = HTTP_POST,
        .handler = realtime_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t realtime_get_uri = {
        .uri = "/realtime",
        .method = HTTP_GET,
        .handler = realtime_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t realtime_stop_uri = {
        .uri = "/realtime/stop",
        .method = HTTP_POST,
        .handler = realtime_stop_handler,
        .user_ctx = NULL
    };

    httpd_uri_t tank_height_uri = {
        .uri = "/tank/height",
        .method = HTTP_GET,
        .handler = tank_height_handler,
        .user_ctx = NULL
    };

    httpd_uri_t real_data_uri = {
        .uri = "/realtime/buffer",
        .method = HTTP_GET,
        .handler = real_data_buffer_handler,
        .user_ctx = NULL
    };

    ESP_LOGI(TAG, "Starting HTTP server on port %d", WEB_SERVER_PORT);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &realtime_post_uri);
        httpd_register_uri_handler(server, &realtime_get_uri);
        httpd_register_uri_handler(server, &realtime_stop_uri);
        httpd_register_uri_handler(server, &tank_height_uri);
        httpd_register_uri_handler(server, &real_data_uri);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

static void stop_webserver(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}

// ============================================================
// WebSocket Broadcast Task
// ============================================================
static void ws_broadcast_task(void *pvParameters) {
    char json[512];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(REAL_DATA_INTERVAL_MS));

        if (ws_client_fd >= 0 && server != NULL) {
            sensor_reading_t reading = sensor_read();
            int len = snprintf(json, sizeof(json),
                "{"
                "\"air_gap_cm\":%.1f,"
                "\"water_depth_cm\":%.1f,"
                "\"valid\":%s,"
                "\"calibrated\":%s,"
                "\"mode\":%d,"
                "\"real_data_index\":%d,"
                "\"timestamp\":%lld"
                "}",
                reading.air_gap_cm,
                reading.water_depth_cm,
                reading.valid ? "true" : "false",
                reading.calibrated ? "true" : "false",
                sensor_get_mode(),
                sensor_real_data_get_index(),
                esp_timer_get_time() / 1000
            );

            httpd_ws_frame_t ws_pkt = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)json,
                .len = len
            };

            esp_err_t ret = httpd_ws_send_frame_async(server, ws_client_fd, &ws_pkt);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_INVALID_ARG) {
                    ws_client_fd = -1;
                }
            }
        }
    }
}
void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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
    printf("Tank Height: %.1f ft (%.1f cm)\n", TANK_HEIGHT_FT, TANK_HEIGHT_CM);
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

    // Initialize WiFi and start webserver
    wifi_init_sta();
    start_webserver();

    // Start WebSocket broadcast task
    xTaskCreate(ws_broadcast_task, "ws_broadcast", 4096, NULL, 5, NULL);

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