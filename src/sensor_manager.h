#ifndef RP2350_SENSOR_MANAGER_H
#define RP2350_SENSOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"
#include "sensors/bme280_port.h"
#include "sensors/tsl2591.h"
#include "sensors/ltr390.h"
#include "sensors/sgp40.h"
#include "sensors/icm20948.h"

typedef struct {
    bool bme280;
    bool tsl2591;
    bool ltr390;
    bool sgp40;
    bool icm20948;
    bool ak09916;   // Embedded magnetometer, requires I2C bypass
} sensor_presence_t;

typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    bool bme280_ok;
    bool tsl2591_ok;
    bool ltr390_ok;
    bool sgp40_ok;
    bool icm20948_ok;
    bme280_reading_t bme280;
    tsl2591_sample_t tsl2591;
    ltr390_sample_t ltr390;
    sgp40_sample_t sgp40;
    icm20948_sample_t icm20948;
} sensor_snapshot_t;

void sensor_manager_init(i2c_inst_t *bus, sensor_presence_t *present);
void sensor_manager_sample(sensor_snapshot_t *snapshot);

#endif
