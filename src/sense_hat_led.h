#ifndef SENSE_HAT_LED_H
#define SENSE_HAT_LED_H

#include <stdbool.h>
#include "hardware/i2c.h"

// Sense HAT V1 (ATtiny88, 0x46). Clears its volatile 8x8 LED matrix.
// Does not write EEPROM or touch sensor registers.
bool sense_hat_led_clear(i2c_inst_t *bus);

#endif
