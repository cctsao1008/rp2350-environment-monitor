#ifndef SENSOR_TSL2591_H
#define SENSOR_TSL2591_H
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"
typedef struct {
    uint16_t full_count;
    uint16_t ir_count;
    uint16_t visible_count;
    uint32_t lux_centi;
    bool saturated;
} tsl2591_sample_t;
bool tsl2591_init(i2c_inst_t *bus);
bool tsl2591_read(tsl2591_sample_t *sample);
#endif
