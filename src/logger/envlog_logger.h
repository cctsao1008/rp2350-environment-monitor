#ifndef RP2350_ENVLOG_LOGGER_H
#define RP2350_ENVLOG_LOGGER_H
#include <stdbool.h>
#include <stdint.h>
#include "sensor_manager.h"

// Single-core, main-loop-only append/poll; never called from USB or I2C IRQ.
void envlog_logger_init(void);
void envlog_logger_append(const sensor_snapshot_t *snapshot);
void envlog_logger_poll(void);
bool envlog_logger_enabled(void);
uint32_t envlog_logger_dropped(void);
#endif
