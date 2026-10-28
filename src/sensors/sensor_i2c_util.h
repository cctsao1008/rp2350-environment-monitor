#ifndef SENSOR_I2C_UTIL_H
#define SENSOR_I2C_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/i2c.h"

bool sensor_i2c_read(i2c_inst_t *bus, uint8_t address,
                     uint8_t reg, uint8_t *data, size_t count);
bool sensor_i2c_write(i2c_inst_t *bus, uint8_t address,
                      uint8_t reg, uint8_t value);
bool sensor_i2c_command(i2c_inst_t *bus, uint8_t address,
                        const uint8_t *data, size_t count);
bool sensor_i2c_receive(i2c_inst_t *bus, uint8_t address,
                        uint8_t *data, size_t count);
uint8_t sensor_crc8(const uint8_t *data, size_t length);

#endif
