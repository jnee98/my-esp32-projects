#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float air_gap_cm;
    float water_depth_cm;
    bool calibrated;
} display_data_t;

bool display_init(void);
void display_update(const display_data_t *data);
void display_show_status(const char *msg, uint32_t duration_ms);
void display_show_calibration(float air_gap);
void display_show_splash(void);
void display_power_off(void);
void display_power_on(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H