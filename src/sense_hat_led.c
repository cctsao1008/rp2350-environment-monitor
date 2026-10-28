#include "sense_hat_led.h"

#include <stdint.h>
#include "hardware/i2c.h"

#define SENSE_HAT_LED_ADDR 0x46u
#define SENSE_HAT_ID_REG  0xF0u
#define SENSE_HAT_ID      0x73u  // 's' in Raspberry Pi ATtiny88 firmware
#define SENSE_HAT_FRAME_BYTES 192u  // 8 rows x 24 bytes (8 pixels x RGB)
#define SENSE_HAT_FRAME_CHUNK 16u
#define I2C_TIMEOUT_US 30000u

bool sense_hat_led_clear(i2c_inst_t *bus) {
    if (bus == NULL) return false;

    // Protect unrelated I2C devices: confirm the controller's identification
    // register before writing any LED framebuffer data.
    const uint8_t id_reg = SENSE_HAT_ID_REG;
    uint8_t id = 0;
    if (i2c_write_timeout_us(bus, SENSE_HAT_LED_ADDR, &id_reg, 1,
                             true, I2C_TIMEOUT_US) != 1 ||
        i2c_read_timeout_us(bus, SENSE_HAT_LED_ADDR, &id, 1,
                            false, I2C_TIMEOUT_US) != 1 ||
        id != SENSE_HAT_ID) {
        return false;
    }

    // Official rpi-sense firmware maps LED data to registers 0..191.
    // Its I2C register pointer auto-increments. Send 12 short transactions,
    // all within the LED area (no EEPROM or control-register writes).
    uint8_t packet[SENSE_HAT_FRAME_CHUNK + 1u] = {0};
    for (unsigned offset = 0; offset < SENSE_HAT_FRAME_BYTES;
         offset += SENSE_HAT_FRAME_CHUNK) {
        packet[0] = (uint8_t)offset;
        int written = i2c_write_timeout_us(
            bus, SENSE_HAT_LED_ADDR, packet, sizeof(packet),
            false, I2C_TIMEOUT_US);
        if (written != (int)sizeof(packet)) return false;
    }
    return true;
}
