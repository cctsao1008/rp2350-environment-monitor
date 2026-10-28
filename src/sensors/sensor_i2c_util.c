#include "sensor_i2c_util.h"

#define SENSOR_I2C_TIMEOUT_US 30000u

bool sensor_i2c_read(i2c_inst_t *bus, uint8_t address,
                     uint8_t reg, uint8_t *data, size_t count) {
    if (bus == NULL || data == NULL || count == 0u) return false;
    if (i2c_write_timeout_us(bus, address, &reg, 1, true,
                             SENSOR_I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(bus, address, data, count, false,
                               SENSOR_I2C_TIMEOUT_US) == (int)count;
}

bool sensor_i2c_write(i2c_inst_t *bus, uint8_t address,
                      uint8_t reg, uint8_t value) {
    uint8_t tx[] = {reg, value};
    return sensor_i2c_command(bus, address, tx, sizeof(tx));
}

bool sensor_i2c_command(i2c_inst_t *bus, uint8_t address,
                        const uint8_t *data, size_t count) {
    if (bus == NULL || data == NULL || count == 0u) return false;
    return i2c_write_timeout_us(bus, address, data, count, false,
                                SENSOR_I2C_TIMEOUT_US) == (int)count;
}

bool sensor_i2c_receive(i2c_inst_t *bus, uint8_t address,
                        uint8_t *data, size_t count) {
    if (bus == NULL || data == NULL || count == 0u) return false;
    return i2c_read_timeout_us(bus, address, data, count, false,
                               SENSOR_I2C_TIMEOUT_US) == (int)count;
}

uint8_t sensor_crc8(const uint8_t *data, size_t length) {
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1u) ^ 0x31u)
                                 : (uint8_t)(crc << 1u);
    }
    return crc;
}
