#include "tsl2591.h"
#include "sensor_i2c_util.h"
#include "pico/time.h"

#define TSL_ADDR 0x29u
#define TSL_COMMAND 0xA0u
#define TSL_ENABLE 0x00u
#define TSL_CONTROL 0x01u
#define TSL_ID 0x12u
#define TSL_CH0 0x14u
#define TSL_CH1 0x16u

// TEMPORARY relative alignment with LTR390 ALS under the current room LED.
// The 2.70 factor is empirical (TSL ~215 lx, LTR ~582 lx); it is NOT an
// absolute lux calibration or a guarantee of agreement under other spectra.
// Keep the original DF=408 algorithm and raw CH0/CH1 for future calibration.
#define TSL2591_RELATIVE_LUX_SCALE 2.70f

static i2c_inst_t *tsl_bus;

bool tsl2591_init(i2c_inst_t *bus) {
    uint8_t id = 0;
    tsl_bus = NULL;
    if (!sensor_i2c_read(bus, TSL_ADDR, TSL_COMMAND | TSL_ID, &id, 1) ||
        id != 0x50u) return false;
    // 100ms integration, 25x gain; ADC on, device powered up.
    if (!sensor_i2c_write(bus, TSL_ADDR, TSL_COMMAND | TSL_CONTROL, 0x10u) ||
        !sensor_i2c_write(bus, TSL_ADDR, TSL_COMMAND | TSL_ENABLE, 0x03u))
        return false;
    tsl_bus = bus;
    sleep_ms(125);
    return true;
}

bool tsl2591_read(tsl2591_sample_t *s) {
    if (!tsl_bus || !s) return false;
    uint8_t ch0[2] = {0}, ch1[2] = {0};
    // Channel 0 must be read before channel 1.
    if (!sensor_i2c_read(tsl_bus, TSL_ADDR, TSL_COMMAND | TSL_CH0, ch0, 2) ||
        !sensor_i2c_read(tsl_bus, TSL_ADDR, TSL_COMMAND | TSL_CH1, ch1, 2))
        return false;
    s->full_count = (uint16_t)(ch0[0] | ((uint16_t)ch0[1] << 8));
    s->ir_count = (uint16_t)(ch1[0] | ((uint16_t)ch1[1] << 8));
    s->visible_count = s->full_count > s->ir_count ?
                       s->full_count - s->ir_count : 0u;
    // 100 ms integration count saturation is 36863 (not 0xFFFF).
    s->saturated = s->full_count >= 36863u || s->ir_count >= 36863u;
    s->lux_centi = 0u;
    if (s->saturated || s->full_count == 0u) return true;

    // Adafruit/ams channel-ratio approximation; lux is indicative, not calibrated.
    const float cpl = (100.0f * 25.0f) / 408.0f;
    float ir_ratio = (float)s->ir_count / (float)s->full_count;
    float lux = ((float)s->full_count - (float)s->ir_count) *
                (1.0f - ir_ratio) / cpl;
    if (lux < 0.0f) lux = 0.0f;
    // Only the reported lux is aligned; sensor ADC readings remain unchanged.
    s->lux_centi = (uint32_t)(lux * TSL2591_RELATIVE_LUX_SCALE * 100.0f + 0.5f);
    return true;
}
