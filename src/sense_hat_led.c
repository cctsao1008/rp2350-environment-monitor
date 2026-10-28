#include "sense_hat_led.h"

#include <stdint.h>
#include "hardware/i2c.h"
#include "pico/time.h"

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

// A reset of RP2350 does not necessarily reset the Sense HAT's ATtiny88.
// Conversely, on power-on the ATtiny88 may load its default LED pattern
// after our earliest successful clear. Blank as soon as I2C is available,
// then repeat during the pre-existing USB CDC connection wait.
//
// Scheduled offsets measured from the first attempt, in milliseconds.
// The last clear is at +3000ms, replacing main()'s old sleep_ms(3000).
// No EEPROM writes or undocumented I2C accesses occur.
// NOTE: This cannot prevent a momentary rainbow BEFORE I2C becomes ready.
bool sense_hat_led_clear_startup(i2c_inst_t *bus) {
    static const uint16_t offsets_ms[] = {
        0u, 50u, 150u, 350u, 750u, 1500u, 3000u
    };
    absolute_time_t started = get_absolute_time();
    bool final_clear_succeeded = false;
    for (unsigned i = 0; i < sizeof(offsets_ms) / sizeof(offsets_ms[0]); ++i) {
        sleep_until(delayed_by_ms(started, offsets_ms[i]));
        // Keep trying even after an early success: ATtiny88 startup may
        // repopulate the framebuffer while RP2350 continues booting.
        final_clear_succeeded = sense_hat_led_clear(bus);
    }
    return final_clear_succeeded;
}
