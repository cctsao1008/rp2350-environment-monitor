#ifndef RP2350_SENSOR_WEB_H
#define RP2350_SENSOR_WEB_H

#include "sensor_manager.h"

// Called on the single cooperative main-loop thread after each 1 Hz sample.
// HTTP never reads I2C; it serves this most recent completed snapshot.
void sensor_web_publish_snapshot(const sensor_snapshot_t *snapshot);

#endif
