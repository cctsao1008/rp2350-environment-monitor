#ifndef RP2350_SENSOR_MANAGER_H
#define RP2350_SENSOR_MANAGER_H

#include <stdint.h>
#include "hardware/i2c.h"
#include "sensors/bme280_port.h"

typedef struct {
    bme280_reading_t bme280;
} sensor_snapshot_t;

// Hardware-I2C bus must already be configured by the caller.
int8_t sensor_manager_init(i2c_inst_t *bus);
int8_t sensor_manager_sample(sensor_snapshot_t *snapshot);

#endif
