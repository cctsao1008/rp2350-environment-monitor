#ifndef SENSOR_ICM20948_H
#define SENSOR_ICM20948_H
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"
typedef struct {
    int32_t accel_mg[3];        // mg (0.001 g), device axes
    int32_t gyro_mdps[3];      // 0.001 degree/sec
    int32_t mag_centi_ut[3];   // 0.01 microtesla; valid when mag_valid
    bool mag_valid;
    bool mag_overflow;
} icm20948_sample_t;
bool icm20948_init(i2c_inst_t *bus);
bool icm20948_read(icm20948_sample_t *sample);
bool icm20948_mag_available(void);
#endif
