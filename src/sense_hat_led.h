#ifndef SENSE_HAT_LED_H
#define SENSE_HAT_LED_H

#include <stdbool.h>
#include "hardware/i2c.h"

// Sense HAT V1 (ATtiny88, 0x46). Clears its volatile 8x8 LED matrix.
// Does not write EEPROM or touch sensor registers.
bool sense_hat_led_clear(i2c_inst_t *bus);

// Boot-time blanking for both RP2350 reset and full power-up. Repeats the
// identity-checked, volatile-framebuffer clear during the existing ~3s
// CDC connect window to handle delayed ATtiny88 initialization.
// Returns true only if the FINAL clear succeeded.
bool sense_hat_led_clear_startup(i2c_inst_t *bus);

#endif
