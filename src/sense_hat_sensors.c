#include "sense_hat_sensors.h"

#include <stddef.h>
#include <stdio.h>
#include "sensors/sensor_i2c_util.h"

#define WHO_AM_I 0x0Fu
#define HTS_ADDR 0x5Fu
#define LPS_ADDR 0x5Cu
#define LSM_AG_ADDR 0x6Au
#define LSM_MAG_ADDR 0x1Cu

// ST's HTS221 and LPS25H need bit 7 set on the I2C subaddress
// to auto-increment through consecutive registers. LSM9DS1 accel/gyro
// uses CTRL_REG8.IF_ADD_INC; magnetometer uses address bit 7.
#define ST_BURST_FLAG 0x80u

static i2c_inst_t *hts_bus;
static i2c_inst_t *lps_bus;
static i2c_inst_t *lsm_bus;

typedef struct {
    int16_t h0_out, h1_out;
    int16_t t0_out, t1_out;
    uint8_t h0_x2, h1_x2;
    uint16_t t0_x8, t1_x8;
} hts_calibration_t;
static hts_calibration_t hts_cal;
static uint32_t hts_invalid_count;

// Exact repeated 0x8000 humidity ADC result warrants separate raw-register
// reads before assuming a calibration defect or a damaged sensing element.
// This is diagnostics only; never modify HTS calibration/EEPROM registers.
static void hts_report_diagnostic(int16_t burst_raw, uint8_t status_before) {
    uint8_t lo = 0, hi = 0, ctrl1 = 0, ctrl2 = 0, av_conf = 0;
    bool ok = sensor_i2c_read(hts_bus, HTS_ADDR, 0x28u, &lo, 1u) &&
              sensor_i2c_read(hts_bus, HTS_ADDR, 0x29u, &hi, 1u) &&
              sensor_i2c_read(hts_bus, HTS_ADDR, 0x20u, &ctrl1, 1u) &&
              sensor_i2c_read(hts_bus, HTS_ADDR, 0x21u, &ctrl2, 1u) &&
              sensor_i2c_read(hts_bus, HTS_ADDR, 0x10u, &av_conf, 1u);
    if (!ok) {
        printf("HTS221 DIAG: register read failed, burst_raw=%d status=0x%02X\r\n",
               (int)burst_raw, (unsigned)status_before);
        return;
    }
    printf("HTS221 DIAG: burst_raw=%d single_L=0x%02X single_H=0x%02X "
           "status=0x%02X CTRL1=0x%02X CTRL2=0x%02X AV_CONF=0x%02X\r\n",
           (int)burst_raw, (unsigned)lo, (unsigned)hi,
           (unsigned)status_before, (unsigned)ctrl1,
           (unsigned)ctrl2, (unsigned)av_conf);
}


static int16_t i16le(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static bool match_id(i2c_inst_t *bus, uint8_t address, uint8_t expected) {
    uint8_t id = 0;
    return sensor_i2c_read(bus, address, WHO_AM_I, &id, 1u) &&
           id == expected;
}
static int32_t div_signed_rounded(int64_t num, int64_t denom) {
    if (denom < 0) { num = -num; denom = -denom; }
    if (num < 0) return (int32_t)(-((-num + denom / 2) / denom));
    return (int32_t)((num + denom / 2) / denom);
}

bool sense_hts221_init(i2c_inst_t *bus) {
    hts_bus = NULL;
    hts_invalid_count = 0;
    if (!match_id(bus, HTS_ADDR, 0xBCu)) return false;
    uint8_t cal[16] = {0};
    if (!sensor_i2c_read(bus, HTS_ADDR, (uint8_t)(0x30u | ST_BURST_FLAG),
                         cal, sizeof(cal))) return false;

    hts_cal.h0_x2 = cal[0];
    hts_cal.h1_x2 = cal[1];
    hts_cal.t0_x8 = (uint16_t)(cal[2] | ((uint16_t)(cal[5] & 0x03u) << 8u));
    hts_cal.t1_x8 = (uint16_t)(cal[3] | ((uint16_t)(cal[5] & 0x0Cu) << 6u));
    hts_cal.h0_out = i16le(&cal[6]);
    hts_cal.h1_out = i16le(&cal[10]);
    hts_cal.t0_out = i16le(&cal[12]);
    hts_cal.t1_out = i16le(&cal[14]);

    if (hts_cal.h0_out == hts_cal.h1_out ||
        hts_cal.t0_out == hts_cal.t1_out) return false;

    // CTRL_REG1 = PD | BDU | 7 Hz (0x80 | 0x04 | 0x02).
    // 7 Hz avoids reporting a stale conversion to the 1 Hz host scheduler.
    if (!sensor_i2c_write(bus, HTS_ADDR, 0x20u, 0x86u)) return false;
    printf("HTS221 CAL: H0x2=%u H1x2=%u H0out=%d H1out=%d\r\n",
           (unsigned)hts_cal.h0_x2, (unsigned)hts_cal.h1_x2,
           (int)hts_cal.h0_out, (int)hts_cal.h1_out);
    hts_bus = bus;
    return true;
}

bool sense_hts221_read(sense_hts221_sample_t *s) {
    if (!hts_bus || !s) return false;
    uint8_t status = 0;
    if (!sensor_i2c_read(hts_bus, HTS_ADDR, 0x27u, &status, 1u) ||
        (status & 0x03u) != 0x03u) return false;
    uint8_t raw[4] = {0};
    if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG), raw, sizeof(raw)))
        return false;

    int16_t humidity_raw = i16le(raw);
    int16_t temperature_raw = i16le(&raw[2]);

    int32_t rh = (int32_t)hts_cal.h0_x2 * 50 +
        div_signed_rounded((int64_t)(humidity_raw - hts_cal.h0_out) *
                           ((int32_t)hts_cal.h1_x2 - hts_cal.h0_x2) * 50,
                           (int32_t)hts_cal.h1_out - hts_cal.h0_out);
    // Preserve unclamped factory interpolation to distinguish a real RH
    // measurement from a value silently clipped to 0% or 100%.
    s->humidity_raw = humidity_raw;
    s->humidity_unclamped_centi_rh = rh;
    s->humidity_valid = rh >= 0 && rh <= 10000 &&
                        humidity_raw != (int16_t)0x8000u;
    if (!s->humidity_valid) {
        ++hts_invalid_count;
        if (hts_invalid_count == 1u || (hts_invalid_count % 60u) == 0u)
            hts_report_diagnostic(humidity_raw, status);
    }
    if (rh < 0) rh = 0;
    if (rh > 10000) rh = 10000;

    s->temp_centi_c = div_signed_rounded(
        (int64_t)hts_cal.t0_x8 * 100, 8) +
        div_signed_rounded(
            (int64_t)(temperature_raw - hts_cal.t0_out) *
            ((int32_t)hts_cal.t1_x8 - hts_cal.t0_x8) * 100,
            (int64_t)8 * (hts_cal.t1_out - hts_cal.t0_out));
    s->humidity_centi_rh = (uint32_t)rh;
    return true;
}

bool sense_lps25h_init(i2c_inst_t *bus) {
    lps_bus = NULL;
    if (!match_id(bus, LPS_ADDR, 0xBDu)) return false;
    // CTRL_REG1 = PD | 7 Hz | BDU.
    if (!sensor_i2c_write(bus, LPS_ADDR, 0x20u, 0xA4u)) return false;
    lps_bus = bus;
    return true;
}

bool sense_lps25h_read(sense_lps25h_sample_t *s) {
    if (!lps_bus || !s) return false;
    uint8_t status = 0;
    if (!sensor_i2c_read(lps_bus, LPS_ADDR, 0x27u, &status, 1u) ||
        (status & 0x03u) != 0x03u) return false;
    uint8_t raw[5] = {0};
    if (!sensor_i2c_read(lps_bus, LPS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG), raw, sizeof(raw)))
        return false;
    int32_t pressure = (int32_t)((uint32_t)raw[0] |
        ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16));
    if (pressure & 0x00800000) pressure |= (int32_t)0xFF000000u;
    s->pressure_centi_hpa = div_signed_rounded((int64_t)pressure * 100, 4096);
    s->temp_centi_c = 4250 +
        div_signed_rounded((int64_t)i16le(&raw[3]) * 100, 480);
    return true;
}

bool sense_lsm9ds1_init(i2c_inst_t *bus) {
    lsm_bus = NULL;
    if (!match_id(bus, LSM_AG_ADDR, 0x68u) ||
        !match_id(bus, LSM_MAG_ADDR, 0x3Du)) return false;

    // A/G: CTRL_REG8 BDU+IF_ADD_INC (0x44).
    // Gyro CTRL_REG1_G: 119 Hz, +/-500 dps, normal bandwidth.
    // Accel CTRL_REG6_XL: 119 Hz, +/-4 g.
    // Magnetometer: temp compensation, medium performance, 20 Hz,
    // +/-4 gauss, continuous conversions, BDU.
    if (!sensor_i2c_write(bus, LSM_AG_ADDR, 0x22u, 0x44u) ||
        !sensor_i2c_write(bus, LSM_AG_ADDR, 0x10u, 0x70u) ||
        !sensor_i2c_write(bus, LSM_AG_ADDR, 0x20u, 0x70u) ||
        !sensor_i2c_write(bus, LSM_MAG_ADDR, 0x20u, 0xB4u) ||
        !sensor_i2c_write(bus, LSM_MAG_ADDR, 0x21u, 0x00u) ||
        !sensor_i2c_write(bus, LSM_MAG_ADDR, 0x22u, 0x00u) ||
        !sensor_i2c_write(bus, LSM_MAG_ADDR, 0x24u, 0x40u)) return false;

    lsm_bus = bus;
    return true;
}

bool sense_lsm9ds1_read(sense_lsm9ds1_sample_t *s) {
    if (!lsm_bus || !s) return false;
    uint8_t ag_status = 0;
    if (!sensor_i2c_read(lsm_bus, LSM_AG_ADDR, 0x17u, &ag_status, 1u) ||
        (ag_status & 0x03u) != 0x03u) return false;

    uint8_t gyr[6] = {0}, accel[6] = {0};
    if (!sensor_i2c_read(lsm_bus, LSM_AG_ADDR, 0x18u, gyr, sizeof(gyr)) ||
        !sensor_i2c_read(lsm_bus, LSM_AG_ADDR, 0x28u, accel, sizeof(accel)))
        return false;
    for (unsigned i = 0; i < 3u; ++i) {
        // Datasheet sensitivities: +/-4g: 0.122 mg/LSB;
        // +/-500dps: 17.5 mdps/LSB. No coordinate alignment applied.
        s->accel_mg[i] = div_signed_rounded(
            (int64_t)i16le(&accel[i * 2u]) * 122, 1000);
        s->gyro_mdps[i] = div_signed_rounded(
            (int64_t)i16le(&gyr[i * 2u]) * 175, 10);
        s->mag_centi_ut[i] = 0;
    }

    s->mag_valid = false;
    uint8_t mag_status = 0;
    if (!sensor_i2c_read(lsm_bus, LSM_MAG_ADDR, 0x27u, &mag_status, 1u) ||
        !(mag_status & 0x08u)) return true;
    uint8_t mag[6] = {0};
    if (!sensor_i2c_read(lsm_bus, LSM_MAG_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG), mag, sizeof(mag)))
        return true;
    for (unsigned i = 0; i < 3u; ++i) {
        // +/-4 gauss: 0.14 mG/LSB = 0.014 uT/LSB = 1.4 centi-uT/LSB.
        s->mag_centi_ut[i] = div_signed_rounded(
            (int64_t)i16le(&mag[i * 2u]) * 14, 10);
    }
    s->mag_valid = true;
    return true;
}
