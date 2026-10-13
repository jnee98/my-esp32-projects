#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"
#include "sdkconfig.h"  // For Kconfig values

// ============================================================
// Hardware Pin Assignments (ESP32-C6 DevKitC-1)
// ============================================================
#define TRIG_PIN              GPIO_NUM_4
#define ECHO_PIN              GPIO_NUM_5
#define OLED_SDA_PIN          GPIO_NUM_8
#define OLED_SCL_PIN          GPIO_NUM_9

// ============================================================
// OLED Settings
// ============================================================
#define OLED_I2C_ADDR         0x3C
#define OLED_WIDTH            128
#define OLED_HEIGHT           64
#define OLED_I2C_FREQ_HZ      400000

// ============================================================
// Tank Configuration (configurable via menuconfig)
// ============================================================
// Default: 55 ft = 1676.4 cm
// Override via: idf.py menuconfig → Water Level Monitor Configuration
#if CONFIG_TANK_HEIGHT_CM > 0
    #define TANK_HEIGHT_CM      CONFIG_TANK_HEIGHT_CM
    #define TANK_HEIGHT_FT      (CONFIG_TANK_HEIGHT_CM / 30.48f)
#else
    #define TANK_HEIGHT_FT      CONFIG_TANK_HEIGHT_FT
    #define TANK_HEIGHT_CM      (CONFIG_TANK_HEIGHT_FT * 30.48f)
#endif

// ============================================================
// Sensor Configuration
// ============================================================
#define ECHO_TIMEOUT_US       30000    // 30ms timeout (~5m range)
#define NUM_SAMPLES           5        // Median filter samples
#define MIN_VALID_DISTANCE_CM 2.0f
#define MAX_VALID_DISTANCE_CM 400.0f   // HC-SR04 max range
#define SAMPLE_DELAY_MS       80       // Between samples

// ============================================================
// Calibration (set via serial after installation)
// ============================================================
// Distance from sensor face to tank bottom (cm)
// 0.0 = uncalibrated (can be overridden at runtime via 'd <cm>')
#define SENSOR_TO_DATUM_CM    CONFIG_SENSOR_TO_DATUM_CM

// ============================================================
// Timing
// ============================================================
#define SENSOR_READ_INTERVAL_MS   100
#define DISPLAY_UPDATE_MS         1000
#define SERIAL_PRINT_MS           1000

// ============================================================
// WiFi Configuration (configurable via menuconfig)
// ============================================================
#define WIFI_SSID               CONFIG_WIFI_SSID
#define WIFI_PASS               CONFIG_WIFI_PASS
#define WIFI_MAX_RETRY          5

// ============================================================
// Real Data Simulation (configurable via menuconfig)
// ============================================================
#define REAL_DATA_BUFFER_SIZE   CONFIG_REAL_DATA_BUFFER_SIZE
#define REAL_DATA_INTERVAL_MS   CONFIG_REAL_DATA_INTERVAL_MS
#define REAL_DATA_TOTAL_MS      (REAL_DATA_BUFFER_SIZE * REAL_DATA_INTERVAL_MS)
#define REAL_DATA_MIN_CM        0.0f
#define REAL_DATA_MAX_CM        TANK_HEIGHT_CM

// ============================================================
// Web Server (configurable via menuconfig)
// ============================================================
#define WEB_SERVER_PORT         CONFIG_WEB_SERVER_PORT
#define WEBSOCKET_URI           "/ws"

// ============================================================
// Operation Modes
// ============================================================
typedef enum {
    MODE_LIVE_SENSOR = 0,      // Live HC-SR04 readings
    MODE_SINE_WAVE = 1,        // Sine wave simulation
    MODE_SQUARE_WAVE = 2,      // Square wave simulation
    MODE_TRIANGLE_WAVE = 3,    // Triangle wave simulation
    MODE_SAWTOOTH_WAVE = 4,    // Sawtooth wave simulation
    MODE_REAL_DATA = 5         // Real data replay (1000 points)
} operation_mode_t;

// ============================================================
// RMT Configuration for precise pulse measurement
// ============================================================
#define RMT_RESOLUTION_HZ     1000000  // 1 MHz = 1 µs resolution
#define RMT_MEM_BLOCKS        1
#define RMT_RX_CHANNEL        0

#endif // CONFIG_H