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

#ifdef __cplusplus
}
#endif

#endif // SENSOR_H