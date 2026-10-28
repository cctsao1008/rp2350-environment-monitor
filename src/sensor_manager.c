#include "sensor_manager.h"
#include <stddef.h>
#include <string.h>
#include "pico/time.h"

static sensor_presence_t connected;
static uint32_t sequence;

void sensor_manager_init(i2c_inst_t *bus, sensor_presence_t *out) {
    memset(&connected, 0, sizeof(connected));
    sequence = 0;

    // Independently initialize each sensor: one failure must not stop
    // reporting measurements from the other devices.
    connected.bme280 = bme280_port_init(bus) == 0;
    connected.tsl2591 = tsl2591_init(bus);
    connected.ltr390 = ltr390_init(bus);
    connected.sgp40 = sgp40_init(bus);
    connected.icm20948 = icm20948_init(bus);
    connected.ak09916 = connected.icm20948 && icm20948_mag_available();
    if (out) *out = connected;
}

void sensor_manager_sample(sensor_snapshot_t *s) {
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->sequence = ++sequence;
    s->uptime_ms = to_ms_since_boot(get_absolute_time());

    if (connected.bme280) {
        s->bme280_ok = bme280_port_read(&s->bme280) == 0;
    }
    if (connected.tsl2591) {
        s->tsl2591_ok = tsl2591_read(&s->tsl2591);
    }
    if (connected.ltr390) {
        s->ltr390_ok = ltr390_read(&s->ltr390);
    }
    if (connected.sgp40 && s->bme280_ok) {
        // Never silently substitute default humidity/temperature: that
        // would misrepresent an uncompensated VOC measurement as valid.
        s->sgp40_ok = sgp40_read(s->bme280.temperature_centi_c,
                                s->bme280.humidity_centi_percent,
                                &s->sgp40);
    }
    if (connected.icm20948) {
        s->icm20948_ok = icm20948_read(&s->icm20948);
    }
}
