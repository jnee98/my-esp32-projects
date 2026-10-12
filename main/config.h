#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"

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
// Sensor Configuration
// ============================================================
#define ECHO_TIMEOUT_US       30000    // 30ms timeout (~5m range)
#define NUM_SAMPLES           5        // Median filter samples
#define MIN_VALID_DISTANCE_CM 2.0f
#define MAX_VALID_DISTANCE_CM 400.0f
#define SAMPLE_DELAY_MS       80       // Between samples

// ============================================================
// Calibration (set via serial after installation)
// ============================================================
// Distance from sensor face to tank bottom (cm)
// 0.0 = uncalibrated
#ifndef SENSOR_TO_DATUM_CM
#define SENSOR_TO_DATUM_CM    0.0f
#endif

// ============================================================
// Timing
// ============================================================
#define SENSOR_READ_INTERVAL_MS   100
#define DISPLAY_UPDATE_MS         1000
#define SERIAL_PRINT_MS           1000

// ============================================================
// RMT Configuration for precise pulse measurement
// ============================================================
#define RMT_RESOLUTION_HZ     1000000  // 1 MHz = 1 µs resolution
#define RMT_MEM_BLOCKS        1
#define RMT_RX_CHANNEL        0

#endif // CONFIG_H