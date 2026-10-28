#ifndef SENSE_HAT_MARQUEE_H
#define SENSE_HAT_MARQUEE_H

#include <stdbool.h>
#include "hardware/i2c.h"

// Optional Sense HAT V1 8x8 scrolling text. Startup default: OFF.
// Run all functions on the main loop (never from an IRQ).
void sense_hat_marquee_init(i2c_inst_t *bus, bool controller_verified);
bool sense_hat_marquee_start(const char *message);
void sense_hat_marquee_stop(void);
void sense_hat_marquee_poll(void);
bool sense_hat_marquee_set_brightness(unsigned level);
bool sense_hat_marquee_available(void);

#endif
