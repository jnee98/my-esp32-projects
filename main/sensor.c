#include "sensor.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "SENSOR";

static float g_datum_cm = SENSOR_TO_DATUM_CM;
static rmt_channel_handle_t rx_channel = NULL;
static SemaphoreHandle_t rx_done_sem = NULL;
static rmt_symbol_word_t rx_symbols[64];
static size_t rx_symbols_count = 0;
static bool rx_ready = false;

static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_woken = pdFALSE;
    rx_symbols_count = edata->num_symbols;
    memcpy(rx_symbols, edata->received_symbols, rx_symbols_count * sizeof(rmt_symbol_word_t));
    rx_ready = true;
    xSemaphoreGiveFromISR(rx_done_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

void sensor_init(void) {
    // Configure TRIG pin as output
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&trig_conf);
    gpio_set_level(TRIG_PIN, 0);

    // Create semaphore for RX done notification
    rx_done_sem = xSemaphoreCreateBinary();
    if (!rx_done_sem) {
        ESP_LOGE(TAG, "Failed to create RX semaphore");
        return;
    }

    // Configure ECHO pin for RMT RX
    rmt_rx_channel_config_t rx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = ECHO_PIN,
        .flags = { .invert_in = false }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_cfg, &rx_channel));

    // Register RX done callback
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = on_rx_done,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    ESP_LOGI(TAG, "Sensor initialized: TRIG=GPIO%d, ECHO=GPIO%d", TRIG_PIN, ECHO_PIN);
}

static float read_single_pulse(void) {
    // Send trigger pulse (10µs HIGH)
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    // Start RMT receive
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 100,       // Filter noise < 100ns
        .signal_range_max_ns = 12000000,  // Max 12ms pulse
    };
    rx_ready = false;
    ESP_ERROR_CHECK(rmt_receive(rx_channel, rx_symbols, sizeof(rx_symbols), &rx_cfg));

    // Wait for RX done callback
    if (xSemaphoreTake(rx_done_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1.0f;  // Timeout
    }

    if (!rx_ready || rx_symbols_count == 0) {
        return -1.0f;
    }

    // Find the HIGH pulse duration (first level=1 symbol)
    uint32_t high_ticks = 0;
    for (size_t i = 0; i < rx_symbols_count; i++) {
        if (rx_symbols[i].level0 == 1) {
            high_ticks = rx_symbols[i].duration0;
            break;
        }
        if (rx_symbols[i].level1 == 1) {
            high_ticks = rx_symbols[i].duration1;
            break;
        }
    }

    if (high_ticks == 0) {
        return -1.0f;
    }

    // Convert ticks to microseconds, then to cm
    // Speed of sound: 343 m/s = 58 µs/cm round-trip
    float duration_us = (float)high_ticks;
    float distance_cm = duration_us / 58.0f;

    return distance_cm;
}

static void sort_float(float *arr, int n) {
    for (int i = 1; i < n; i++) {
        float key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

sensor_reading_t sensor_read(void) {
    sensor_reading_t result = {
        .air_gap_cm = -1.0f,
        .water_depth_cm = -1.0f,
        .valid = false,
        .calibrated = (g_datum_cm > 0.0f)
    };

    float samples[NUM_SAMPLES];
    int valid_count = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        float d = read_single_pulse();
        
        if (d >= MIN_VALID_DISTANCE_CM && d <= MAX_VALID_DISTANCE_CM) {
            samples[valid_count++] = d;
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
    }

    if (valid_count < 3) {
        ESP_LOGW(TAG, "Insufficient valid samples: %d/%d", valid_count, NUM_SAMPLES);
        return result;
    }

    sort_float(samples, valid_count);
    float median = samples[valid_count / 2];

    result.air_gap_cm = median;
    result.valid = true;

    if (result.calibrated) {
        float depth = g_datum_cm - median;
        if (depth >= 0) {
            result.water_depth_cm = depth;
        } else {
            result.water_depth_cm = -1.0f;  // Below datum (overflow)
        }
    }

    return result;
}

bool sensor_is_calibrated(void) {
    return g_datum_cm > 0.0f;
}

void sensor_set_datum(float datum_cm) {
    if (datum_cm > 0.0f) {
        g_datum_cm = datum_cm;
        ESP_LOGI(TAG, "Calibration datum set to %.1f cm", g_datum_cm);
    }
}

float sensor_get_datum(void) {
    return g_datum_cm;
}