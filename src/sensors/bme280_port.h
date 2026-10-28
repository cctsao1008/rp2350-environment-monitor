#ifndef RP2350_BME280_PORT_H
#define RP2350_BME280_PORT_H

#include <stdint.h>
#include "hardware/i2c.h"

// Fixed-point units avoid optional floating-point printf support on embedded libc.
typedef struct {
    int32_t temperature_centi_c;       // 0.01 degrees Celsius
    uint32_t humidity_centi_percent;   // 0.01 %RH
    uint32_t pressure_pa;              // Pascals (1 Pa)
} bme280_reading_t;

// Returns BME280_OK (0) on success; otherwise Bosch API error code.
// Initializes calibration and configures forced-mode measurements.
int8_t bme280_port_init(i2c_inst_t *bus);

// Triggers one new measurement and returns Bosch-compensated physical values.
int8_t bme280_port_read(bme280_reading_t *reading);

#endif
