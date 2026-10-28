#include "sense_hat_probe.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "hardware/i2c.h"

#define SENSE_HAT_WHO_AM_I 0x0Fu
#define I2C_TIMEOUT_US 30000u

typedef struct {
    const char *name;
    uint8_t address;
    uint8_t expected_id;
} sensor_id_target_t;

// Sense HAT V1 (official Raspberry Pi Rev 1 board). Do not probe 0x46
// here: its ATtiny88 controller is verified separately by sense_hat_led.c.
static const sensor_id_target_t targets[] = {
    {"LSM9DS1_AG", 0x6Au, 0x68u},
    {"LSM9DS1_MAG", 0x1Cu, 0x3Du},
    {"LPS25H", 0x5Cu, 0xBDu},
    {"HTS221", 0x5Fu, 0xBCu}
};

static bool read_id(i2c_inst_t *bus, uint8_t address, uint8_t *id) {
    const uint8_t reg = SENSE_HAT_WHO_AM_I;
    if (i2c_write_timeout_us(bus, address, &reg, 1, true,
                             I2C_TIMEOUT_US) != 1) {
        return false;
    }
    return i2c_read_timeout_us(bus, address, id, 1, false,
                               I2C_TIMEOUT_US) == 1;
}

bool sense_hat_probe_identify(i2c_inst_t *bus) {
    if (bus == NULL) {
        printf("Sense HAT V1 sensor ID: I2C bus unavailable\r\n");
        return false;
    }

    unsigned passed = 0;
    const unsigned total = (unsigned)(sizeof(targets) / sizeof(targets[0]));
    printf("Sense HAT V1 sensor identities (WHO_AM_I 0x0F):\r\n");

    for (unsigned i = 0; i < total; ++i) {
        const sensor_id_target_t *target = &targets[i];
        uint8_t id = 0u;

        if (!read_id(bus, target->address, &id)) {
            printf("  %-12s at 0x%02X: NO_RESPONSE (expected 0x%02X)\r\n",
                   target->name, target->address, target->expected_id);
            continue;
        }

        const bool match = id == target->expected_id;
        printf("  %-12s at 0x%02X: ID 0x%02X (%s; expected 0x%02X)\r\n",
               target->name, target->address, id,
               match ? "PASS" : "UNEXPECTED", target->expected_id);
        if (match) {
            ++passed;
        }
    }

    printf("Sense HAT V1 sensor ID summary: %u/%u PASS\r\n", passed, total);
    return passed == total;
}
