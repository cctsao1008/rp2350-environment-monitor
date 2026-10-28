#include "icm20948.h"
#include "sensor_i2c_util.h"
#include "pico/time.h"

#define ICM_ADDR 0x68u
#define AK_ADDR 0x0Cu
#define ICM_BANK_SEL 0x7Fu
#define ICM_WHO_AM_I 0x00u
#define ICM_USER_CTRL 0x03u
#define ICM_PWR_MGMT_1 0x06u
#define ICM_PWR_MGMT_2 0x07u
#define ICM_INT_PIN_CFG 0x0Fu
#define ICM_ACCEL_XOUT_H 0x2Du
#define AK_WIA1 0x00u
#define AK_ST1 0x10u
#define AK_CNTL2 0x31u
static i2c_inst_t *icm_bus;
static bool mag_ready;

static bool bank(uint8_t index) {
    return sensor_i2c_write(icm_bus, ICM_ADDR,
                            ICM_BANK_SEL, (uint8_t)(index << 4));
}
static bool reg(uint8_t address, uint8_t value) {
    return sensor_i2c_write(icm_bus, ICM_ADDR, address, value);
}
static int16_t be16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static int16_t le16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[1] << 8) | p[0]);
}
static int32_t scale_signed(int16_t raw, int32_t numerator, int32_t denominator) {
    int32_t value = (int32_t)raw * numerator;
    return value >= 0 ? (value + denominator / 2) / denominator
                      : -((-value + denominator / 2) / denominator);
}

bool icm20948_init(i2c_inst_t *bus) {
    icm_bus = bus;
    mag_ready = false;
    uint8_t id = 0;
    if (!bank(0) ||
        !sensor_i2c_read(bus, ICM_ADDR, ICM_WHO_AM_I, &id, 1) ||
        id != 0xEAu) goto fail;

    if (!reg(ICM_PWR_MGMT_1, 0x80u)) goto fail; // soft reset
    sleep_ms(100);
    if (!bank(0) || !reg(ICM_PWR_MGMT_1, 0x01u) ||
        !reg(ICM_PWR_MGMT_2, 0x00u)) goto fail;
    sleep_ms(50);
    // Bank 2: gyro ±500 dps, accel ±4 g, 65.5 LSB/dps, 8192 LSB/g.
    if (!bank(2) || !reg(0x01u, 0x02u) || !reg(0x14u, 0x02u) ||
        !bank(0)) goto fail;

    // BYPASS_EN exposes the internal AK09916 (0x0C) to the same host I2C bus.
    // If the HAT wiring prevents bypass, accel+gyro remain usable.
    if (!reg(ICM_USER_CTRL, 0x00u) || !reg(ICM_INT_PIN_CFG, 0x02u))
        return true;
    sleep_ms(20);
    uint8_t mag_id[2] = {0};
    if (sensor_i2c_read(bus, AK_ADDR, AK_WIA1, mag_id, 2) &&
        mag_id[0] == 0x48u && mag_id[1] == 0x09u &&
        sensor_i2c_write(bus, AK_ADDR, AK_CNTL2, 0x08u)) {
        sleep_ms(15);
        mag_ready = true;      // AK09916 continuous measurement at 100 Hz
    }
    return true;

fail:
    icm_bus = NULL;
    return false;
}

bool icm20948_mag_available(void) { return mag_ready; }

bool icm20948_read(icm20948_sample_t *s) {
    if (!icm_bus || !s || !bank(0)) return false;
    uint8_t raw[12] = {0};
    if (!sensor_i2c_read(icm_bus, ICM_ADDR, ICM_ACCEL_XOUT_H,
                         raw, sizeof(raw))) return false;
    for (unsigned i = 0; i < 3; ++i) {
        s->accel_mg[i] = scale_signed(be16(&raw[2u * i]), 1000, 8192);
        // 65.5 LSB/dps at +/-500 dps, exactly 2000/131 mdps per LSB.
        s->gyro_mdps[i] = scale_signed(be16(&raw[6u + 2u * i]), 2000, 131);
        s->mag_centi_ut[i] = 0;
    }
    s->mag_valid = false;
    s->mag_overflow = false;
    if (!mag_ready) return true;

    // Read ST1 through ST2 in one transaction; ST2 releases the data latch.
    uint8_t mag[9] = {0};
    if (!sensor_i2c_read(icm_bus, AK_ADDR, AK_ST1, mag, sizeof(mag)))
        return true;
    s->mag_overflow = (mag[8] & 0x08u) != 0u;
    if (!(mag[0] & 0x01u) || s->mag_overflow) return true;
    for (unsigned i = 0; i < 3u; ++i)
        s->mag_centi_ut[i] = (int32_t)le16(&mag[1u + 2u * i]) * 15;
    // AK09916 sensitivity: 0.15 uT/LSB = 15 centi-uT/LSB.
    s->mag_valid = true;
    return true;
}
