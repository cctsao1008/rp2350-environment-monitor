#ifndef RP2350_SENSE_HAT_PROBE_H
#define RP2350_SENSE_HAT_PROBE_H

#include <stdbool.h>
#include "hardware/i2c.h"

// Read-only Sense HAT V1 sensor identity check.
// Reports each result over USB CDC; no sensor configuration is changed.
bool sense_hat_probe_identify(i2c_inst_t *bus);

#endif
