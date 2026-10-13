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
#include <stdlib.h>

static const char *TAG = "SENSOR";

static float g_datum_cm = SENSOR_TO_DATUM_CM;
static rmt_channel_handle_t rx_channel = NULL;
static SemaphoreHandle_t rx_done_sem = NULL;
static rmt_symbol_word_t rx_symbols[64];
static size_t rx_symbols_count = 0;
static bool rx_ready = false;

static float *g_real_data_buffer = NULL;
static int g_real_data_index = 0;
static bool g_real_data_running = false;
static operation_mode_t g_current_mode = MODE_LIVE_SENSOR;

static float g_sine_phase = 0.0f;
static float g_sine_amplitude = 0.0f;
static float g_sine_offset = 0.0f;
static float g_sine_frequency = 0.01f;

static float g_square_phase = 0.0f;
static float g_square_period = 2000.0f;
static float g_triangle_phase = 0.0f;
static float g_triangle_period = 2000.0f;
static float g_sawtooth_phase = 0.0f;
static float g_sawtooth_period = 2000.0f;

static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_woken = pdFALSE;
    rx_symbols_count = edata->num_symbols;
    memcpy(rx_symbols, edata->received_symbols, rx_symbols_count * sizeof(rmt_symbol_word_t));
    rx_ready = true;
    xSemaphoreGiveFromISR(rx_done_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

void sensor_init(void) {
    g_sine_amplitude = TANK_HEIGHT_CM / 2.0f;
    g_sine_offset = TANK_HEIGHT_CM / 2.0f;
    
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << TRIG_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&trig_conf);
    gpio_set_level(TRIG_PIN, 0);

    rx_done_sem = xSemaphoreCreateBinary();
    if (!rx_done_sem) {
        ESP_LOGE(TAG, "Failed to create RX semaphore");
        return;
    }

    rmt_rx_channel_config_t rx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = ECHO_PIN,
        .flags = { .invert_in = false }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_cfg, &rx_channel));

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    ESP_LOGI(TAG, "Sensor initialized: TRIG=GPIO%d, ECHO=GPIO%d", TRIG_PIN, ECHO_PIN);
}

static float read_single_pulse(void) {
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 100,
        .signal_range_max_ns = 12000000,
    };
    rx_ready = false;
    ESP_ERROR_CHECK(rmt_receive(rx_channel, rx_symbols, sizeof(rx_symbols), &rx_cfg));

    if (xSemaphoreTake(rx_done_sem, pdMS_TO_TICKS(50)) != pdTRUE) return -1.0f;
    if (!rx_ready || rx_symbols_count == 0) return -1.0f;

    uint32_t high_ticks = 0;
    for (size_t i = 0; i < rx_symbols_count; i++) {
        if (rx_symbols[i].level0 == 1) { high_ticks = rx_symbols[i].duration0; break; }
        if (rx_symbols[i].level1 == 1) { high_ticks = rx_symbols[i].duration1; break; }
    }
    if (high_ticks == 0) return -1.0f;
    return (float)high_ticks / 58.0f;
}

static void sort_float(float *arr, int n) {
    for (int i = 1; i < n; i++) {
        float key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
}

sensor_reading_t sensor_read(void) {
    if (g_current_mode != MODE_LIVE_SENSOR) return sensor_read_simulated();

    sensor_reading_t result = { .air_gap_cm = -1.0f, .water_depth_cm = -1.0f,
        .valid = false, .calibrated = (g_datum_cm > 0.0f) };

    float samples[NUM_SAMPLES]; int valid_count = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        float d = read_single_pulse();
        if (d >= MIN_VALID_DISTANCE_CM && d <= MAX_VALID_DISTANCE_CM) samples[valid_count++] = d;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
    }
    if (valid_count < 3) { ESP_LOGW(TAG, "Insufficient valid samples: %d/%d", valid_count, NUM_SAMPLES); return result; }
    sort_float(samples, valid_count);
    float median = samples[valid_count / 2];

    result.air_gap_cm = median; result.valid = true;
    if (result.calibrated) {
        float depth = g_datum_cm - median;
        result.water_depth_cm = (depth >= 0) ? depth : -1.0f;
    }
    return result;
}

bool sensor_is_calibrated(void) { return g_datum_cm > 0.0f; }
void sensor_set_datum(float datum_cm) { if (datum_cm > 0.0f) { g_datum_cm = datum_cm; ESP_LOGI(TAG, "Datum set to %.1f cm", g_datum_cm); } }
float sensor_get_datum(void) { return g_datum_cm; }

void sensor_real_data_init(void) {
    if (g_real_data_buffer == NULL) {
        g_real_data_buffer = (float *)malloc(REAL_DATA_BUFFER_SIZE * sizeof(float));
        if (!g_real_data_buffer) { ESP_LOGE(TAG, "Malloc failed"); return; }
        for (int i = 0; i < REAL_DATA_BUFFER_SIZE; i++) {
            float t = (float)i / REAL_DATA_BUFFER_SIZE * 2.0f * M_PI;
            g_real_data_buffer[i] = g_sine_amplitude * sinf(t) + g_sine_offset;
            if (g_real_data_buffer[i] < REAL_DATA_MIN_CM) g_real_data_buffer[i] = REAL_DATA_MIN_CM;
            if (g_real_data_buffer[i] > REAL_DATA_MAX_CM) g_real_data_buffer[i] = REAL_DATA_MAX_CM;
        }
    }
    g_real_data_index = 0; g_real_data_running = true;
    ESP_LOGI(TAG, "Real data started: %d samples", REAL_DATA_BUFFER_SIZE);
}

void sensor_real_data_deinit(void) { if (g_real_data_buffer) { free(g_real_data_buffer); g_real_data_buffer = NULL; } g_real_data_running = false; g_real_data_index = 0; }
bool sensor_real_data_is_running(void) { return g_real_data_running; }
int sensor_real_data_get_index(void) { return g_real_data_index; }
int sensor_real_data_get_buffer_size(void) { return REAL_DATA_BUFFER_SIZE; }
float sensor_real_data_get_value(int index) { return (g_real_data_buffer && index >= 0 && index < REAL_DATA_BUFFER_SIZE) ? g_real_data_buffer[index] : -1.0f; }
void sensor_real_data_set_value(int index, float value_cm) { if (g_real_data_buffer && index >= 0 && index < REAL_DATA_BUFFER_SIZE) { if (value_cm < REAL_DATA_MIN_CM) value_cm = REAL_DATA_MIN_CM; if (value_cm > REAL_DATA_MAX_CM) value_cm = REAL_DATA_MAX_CM; g_real_data_buffer[index] = value_cm; } }

void sensor_set_mode(operation_mode_t mode) { if (mode == MODE_REAL_DATA) sensor_real_data_init(); else sensor_real_data_deinit(); g_current_mode = mode; ESP_LOGI(TAG, "Mode set to %d", mode); }
operation_mode_t sensor_get_mode(void) { return g_current_mode; }

sensor_reading_t sensor_read_simulated(void) {
    sensor_reading_t result = { .air_gap_cm = -1.0f, .water_depth_cm = -1.0f, .valid = false, .calibrated = (g_datum_cm > 0.0f) };
    float air_gap = -1.0f;

    switch (g_current_mode) {
        case MODE_SINE_WAVE:
            g_sine_phase += g_sine_frequency; if (g_sine_phase >= 2.0f * M_PI) g_sine_phase -= 2.0f * M_PI;
            air_gap = g_sine_amplitude * sinf(g_sine_phase) + g_sine_offset; break;
        case MODE_SQUARE_WAVE:
            g_square_phase += (2.0f * M_PI * REAL_DATA_INTERVAL_MS) / g_square_period; if (g_square_phase >= 2.0f * M_PI) g_square_phase -= 2.0f * M_PI;
            air_gap = (sinf(g_square_phase) >= 0) ? g_sine_offset + g_sine_amplitude : g_sine_offset - g_sine_amplitude; break;
        case MODE_TRIANGLE_WAVE:
            g_triangle_phase += (4.0f * g_sine_amplitude * REAL_DATA_INTERVAL_MS) / g_triangle_period; if (g_triangle_phase > 4.0f * g_sine_amplitude) g_triangle_phase -= 4.0f * g_sine_amplitude;
            { float half = 2.0f * g_sine_amplitude; air_gap = (g_triangle_phase <= half) ? g_sine_offset - g_sine_amplitude + g_triangle_phase : g_sine_offset + g_sine_amplitude - (g_triangle_phase - half); } break;
        case MODE_SAWTOOTH_WAVE:
            g_sawtooth_phase += (2.0f * g_sine_amplitude * REAL_DATA_INTERVAL_MS) / g_sawtooth_period; if (g_sawtooth_phase > 2.0f * g_sine_amplitude) g_sawtooth_phase -= 2.0f * g_sine_amplitude;
            air_gap = g_sine_offset - g_sine_amplitude + g_sawtooth_phase; break;
        case MODE_REAL_DATA:
            if (g_real_data_buffer && g_real_data_running) { air_gap = g_real_data_buffer[g_real_data_index]; g_real_data_index = (g_real_data_index + 1) % REAL_DATA_BUFFER_SIZE; }
            else air_gap = TANK_HEIGHT_CM / 2.0f; break;
        default: air_gap = TANK_HEIGHT_CM / 2.0f; break;
    }

    if (air_gap < REAL_DATA_MIN_CM) air_gap = REAL_DATA_MIN_CM;
    if (air_gap > REAL_DATA_MAX_CM) air_gap = REAL_DATA_MAX_CM;
    result.air_gap_cm = air_gap; result.valid = true;
    if (result.calibrated) { float depth = g_datum_cm - air_gap; result.water_depth_cm = (depth >= 0) ? depth : -1.0f; }
    return result;
}
