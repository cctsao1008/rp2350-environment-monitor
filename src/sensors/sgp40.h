#ifndef SENSOR_SGP40_H
#define SENSOR_SGP40_H
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"
typedef struct {
    uint16_t sraw_voc;      // Raw MOX signal, not ppm or TVOC concentration
    int32_t voc_index;     // 0 during initialization blackout; otherwise 1..500
    bool index_ready;
} sgp40_sample_t;
bool sgp40_init(i2c_inst_t *bus);
bool sgp40_read(int32_t temperature_centi_c,
                uint32_t humidity_centi_rh,
                sgp40_sample_t *sample);
#endif
