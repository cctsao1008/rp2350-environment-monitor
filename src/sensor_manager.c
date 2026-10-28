#include "sensor_manager.h"

#include <stddef.h>
#include "bme280.h"

// Initial P1 sensor manager: one physical data source.
// Keep protocol logic inside each sensor-specific adapter.
int8_t sensor_manager_init(i2c_inst_t *bus) {
    return bme280_port_init(bus);
}

int8_t sensor_manager_sample(sensor_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return BME280_E_NULL_PTR;
    }
    return bme280_port_read(&snapshot->bme280);
}
