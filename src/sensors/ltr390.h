#ifndef SENSOR_LTR390_H
#define SENSOR_LTR390_H
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"
typedef struct {
    uint32_t uvs_counts;
    uint32_t als_counts;
    uint32_t uvi_centi;
    uint32_t lux_centi;
} ltr390_sample_t;
bool ltr390_init(i2c_inst_t *bus);
bool ltr390_read(ltr390_sample_t *sample);
#endif
