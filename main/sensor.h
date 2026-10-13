#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float air_gap_cm;
    float water_depth_cm;
    bool valid;
    bool calibrated;
} sensor_reading_t;

void sensor_init(void);
sensor_reading_t sensor_read(void);
bool sensor_is_calibrated(void);
void sensor_set_datum(float datum_cm);
float sensor_get_datum(void);

// Real data simulation
void sensor_real_data_init(void);
void sensor_real_data_deinit(void);
bool sensor_real_data_is_running(void);
int sensor_real_data_get_index(void);
int sensor_real_data_get_buffer_size(void);
float sensor_real_data_get_value(int index);
void sensor_real_data_set_value(int index, float value_cm);

// Operation mode
void sensor_set_mode(operation_mode_t mode);
operation_mode_t sensor_get_mode(void);
sensor_reading_t sensor_read_simulated(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_H