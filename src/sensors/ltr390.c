#include "ltr390.h"
#include "sensor_i2c_util.h"
#include "pico/time.h"

#define LTR_ADDR 0x53u
#define LTR_CTRL 0x00u
#define LTR_RATE 0x04u
#define LTR_GAIN 0x05u
#define LTR_PART_ID 0x06u
#define LTR_STATUS 0x07u
#define LTR_ALS_DATA 0x0Du
#define LTR_UVS_DATA 0x10u
static i2c_inst_t *ltr_bus;

static bool mode(uint8_t ctrl) {
    return sensor_i2c_write(ltr_bus, LTR_ADDR, LTR_CTRL, ctrl);
}
static bool get24(uint8_t reg, uint32_t *value) {
    uint8_t x[3] = {0}, status = 0;
    if (!sensor_i2c_read(ltr_bus, LTR_ADDR, LTR_STATUS, &status, 1) ||
        !(status & 0x08u)) return false;
    if (!sensor_i2c_read(ltr_bus, LTR_ADDR, reg, x, 3)) return false;
    *value = (uint32_t)x[0] | ((uint32_t)x[1] << 8u) |
             ((uint32_t)x[2] << 16u);
    return true;
}

bool ltr390_init(i2c_inst_t *bus) {
    uint8_t id = 0;
    ltr_bus = NULL;
    if (!sensor_i2c_read(bus, LTR_ADDR, LTR_PART_ID, &id, 1) ||
        (id >> 4) != 0x0Bu) return false;
    ltr_bus = bus;
    // Gain 3x, 18-bit / 100ms integration, 100ms measurement rate.
    if (!sensor_i2c_write(bus, LTR_ADDR, LTR_GAIN, 0x01u) ||
        !sensor_i2c_write(bus, LTR_ADDR, LTR_RATE, 0x22u) ||
        !mode(0x0Au)) { ltr_bus = NULL; return false; }
    sleep_ms(130);
    return true;
}

bool ltr390_read(ltr390_sample_t *s) {
    if (!ltr_bus || !s) return false;
    // UV mode runs between cycles; switch to ALS, then return to UV.
    uint32_t uv = 0, als = 0;
    if (!get24(LTR_UVS_DATA, &uv)) return false;
    if (!mode(0x02u)) return false;     // ALS mode, sensor enabled
    sleep_ms(130);
    bool ok = get24(LTR_ALS_DATA, &als);
    bool restored = mode(0x0Au);       // UV mode for next 1 Hz cycle
    if (!ok || !restored) return false;
    s->uvs_counts = uv;
    s->als_counts = als;
    // Sensitivity at 18-bit (100ms) / gain 3x: ~95.83 counts per UVI.
    s->uvi_centi = (uint32_t)(((uint64_t)uv * 100u * 12u + 575u) / 1150u);
    // LTR390 datasheet lux = 0.6 * ALS / (gain * integration/100ms).
    s->lux_centi = (uint32_t)((uint64_t)als * 20u);  // 0.2 lux/count -> centilux
    return true;
}
