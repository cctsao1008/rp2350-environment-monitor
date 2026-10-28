#include "sense_hat_sensors.h"

#include <stddef.h>
#include <stdio.h>
#include "pico/time.h"
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

// Automatic bounded heater experiment, NOT a continuous recovery service.
// See ST HTS221 DS section 7.4: read neither T nor RH while heater ON.
#define HTS_AUTO_BASELINE_MS 5000u
#define HTS_AUTO_HEAT_MS 10000u
#define HTS_AUTO_COOL_MS 30000u
#define HTS_AUTO_LOG_MS 5000u
#define HTS_AUTO_MAX_CYCLES 3u
// Hardware evaluation finished (2026-10-28): three heater cycles did not
// change RH raw 0x8000. Keep diagnostic code for reproducibility, but
// disable heating on every new boot. 5-second observation remains automatic.
#define HTS_AUTO_ENABLE_HEATING 0
#define HTS_AUTO_OFF_RETRY_MS 250u

typedef enum {
    HTS_AUTO_DISABLED = 0,
    HTS_AUTO_BASELINE,
    HTS_AUTO_HEATING,
    HTS_AUTO_COOLING,
    HTS_AUTO_OBSERVE_ONLY,
    HTS_AUTO_DONE,
    HTS_AUTO_OFF_FAULT
} hts_auto_phase_t;

static hts_auto_phase_t hts_auto_phase;
static uint32_t hts_auto_deadline_ms;
static uint32_t hts_auto_next_log_ms;
static uint32_t hts_auto_next_off_retry_ms;
static unsigned hts_auto_cycles_completed;
static bool hts_auto_have_raw;
static bool hts_auto_last_rh_valid;
static int16_t hts_auto_last_hraw;
static int16_t hts_auto_last_traw;

static bool hts_auto_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static bool hts_auto_read_ctrl(uint8_t *ctrl1, uint8_t *ctrl2) {
    return hts_bus &&
           sensor_i2c_read(hts_bus, HTS_ADDR, 0x20u, ctrl1, 1u) &&
           sensor_i2c_read(hts_bus, HTS_ADDR, 0x21u, ctrl2, 1u);
}

// Write OFF, then verify that bit1 is zero. Never write calibration.
static bool hts_auto_heater_off(void) {
    uint8_t ctrl2 = 0u;
    if (!hts_bus ||
        !sensor_i2c_read(hts_bus, HTS_ADDR, 0x21u, &ctrl2, 1u))
        return false;
    if ((ctrl2 & 0x02u) != 0u) {
        if (!sensor_i2c_write(hts_bus, HTS_ADDR,
                              0x21u, (uint8_t)(ctrl2 & ~0x02u)))
            return false;
    }
    return sensor_i2c_read(hts_bus, HTS_ADDR, 0x21u, &ctrl2, 1u) &&
           (ctrl2 & 0x02u) == 0u;
}

static void hts_auto_enter_off_fault(uint32_t now) {
    hts_auto_phase = HTS_AUTO_OFF_FAULT;
    hts_auto_next_off_retry_ms = now;
    printf("HTS221 AUTO: HEATER_OFF_UNCONFIRMED; "
           "retrying every 250ms; POWER-DISCONNECT if this persists!\r\n");
}


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


/*
 * Capture untouched HTS221 configuration BEFORE sense_hts221_init()
 * writes CTRL_REG1. Does NOT modify any I2C device registers.
 * HTS221 Rev5 Table15 nominal power-on: AV_CONF=1B CTRL1/2/3=00.
 * A value differing from default is NOT proof of a sensor fault:
 * a reset of RP2350 need not power-cycle the sensor.
 */
void sense_hts221_preinit_snapshot(i2c_inst_t *bus) {
    if (!bus) {
        printf("HTS221 PREINIT: ERROR bus unavailable\r\n");
        return;
    }
    const uint8_t addrs[] = {0x0Fu, 0x10u, 0x20u, 0x21u, 0x22u};
    uint8_t value[sizeof(addrs)] = {0};
    for (unsigned i = 0u; i < sizeof(addrs); ++i) {
        if (!sensor_i2c_read(bus, HTS_ADDR, addrs[i], &value[i], 1u)) {
            printf("HTS221 PREINIT: READ_FAILED reg=0x%02X\r\n",
                   (unsigned)addrs[i]);
            return;
        }
    }
    printf("HTS221 PREINIT: ID=%02X AV_CONF=%02X CTRL1=%02X "
           "CTRL2=%02X CTRL3=%02X EXPECTED_POWER_ON=BC/1B/00/00/00 "
           "REG_DEFAULTS=%s (reset type not inferred)\r\n",
           (unsigned)value[0], (unsigned)value[1],
           (unsigned)value[2], (unsigned)value[3], (unsigned)value[4],
           value[0] == 0xBCu && value[1] == 0x1Bu &&
           value[2] == 0u && value[3] == 0u && value[4] == 0u ?
                "MATCH" : "DIFFER");
}

bool sense_hts221_init(i2c_inst_t *bus) {
    hts_bus = NULL;
    hts_invalid_count = 0;
    hts_auto_phase = HTS_AUTO_DISABLED;
    hts_auto_have_raw = false;
    hts_auto_last_rh_valid = false;
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
    if (!hts_bus || !s ||
        hts_auto_phase == HTS_AUTO_HEATING ||
        hts_auto_phase == HTS_AUTO_COOLING ||
        hts_auto_phase == HTS_AUTO_OFF_FAULT) return false;
    uint8_t status = 0;
    if (!sensor_i2c_read(hts_bus, HTS_ADDR, 0x27u, &status, 1u) ||
        (status & 0x03u) != 0x03u) return false;
    uint8_t raw[4] = {0};
    if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG), raw, sizeof(raw)))
        return false;

    int16_t humidity_raw = i16le(raw);
    int16_t temperature_raw = i16le(&raw[2]);
    hts_auto_have_raw = true;
    hts_auto_last_hraw = humidity_raw;
    hts_auto_last_traw = temperature_raw;

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
    hts_auto_last_rh_valid = s->humidity_valid;
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


/*
 * HTS221 fault-isolation path, guided by ST DocID026333 Rev 5.
 * Neither diagnostic command changes calibration registers 0x30..0x3F,
 * reboots trim memory, nor turns on the heater.
 *
 * hts diag    : read only, capture the register contents as delivered.
 * hts oneshot : requires exact known-good 0x86/0x00 control configuration;
 *               briefly use 0x84 + ONE_SHOT and restore 7 Hz afterwards.
 */
static bool hts_diag_read(uint8_t reg, uint8_t *out) {
    return sensor_i2c_read(hts_bus, HTS_ADDR, reg, out, 1u);
}

static void hts_diag_print_cal(void) {
    uint8_t cal[16] = {0};
    if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x30u | ST_BURST_FLAG),
                         cal, sizeof(cal))) {
        printf("HTS221 CAL DIAG: READ_FAILED\r\n");
        return;
    }
    printf("HTS221 CAL DIAG: bytes[30..3F]=");
    for (unsigned i = 0; i < sizeof(cal); ++i)
        printf("%s%02X", i ? " " : "", (unsigned)cal[i]);
    printf("\r\n");
    printf("HTS221 CAL DIAG: H0x2=%u H1x2=%u H0out=%d H1out=%d "
           "T0x8=%u T1x8=%u T0out=%d T1out=%d\r\n",
           (unsigned)hts_cal.h0_x2, (unsigned)hts_cal.h1_x2,
           (int)hts_cal.h0_out, (int)hts_cal.h1_out,
           (unsigned)hts_cal.t0_x8, (unsigned)hts_cal.t1_x8,
           (int)hts_cal.t0_out, (int)hts_cal.t1_out);
}

static bool hts_diag_read_measurements(const char *label) {
    uint8_t status_before = 0, burst[4] = {0};
    uint8_t lo = 0, hi = 0;
    if (!hts_diag_read(0x27u, &status_before) ||
        !sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG),
                         burst, sizeof(burst)) ||
        !hts_diag_read(0x28u, &lo) || !hts_diag_read(0x29u, &hi)) {
        printf("HTS221 %s: output registers READ_FAILED\r\n", label);
        return false;
    }
    printf("HTS221 %s: status_before=0x%02X H_DA=%u T_DA=%u "
           "burst=%02X %02X %02X %02X Hraw=%d Traw=%d "
           "single_H=%02X%02X\r\n",
           label, (unsigned)status_before,
           (unsigned)((status_before >> 1u) & 1u),
           (unsigned)(status_before & 1u),
           (unsigned)burst[0], (unsigned)burst[1],
           (unsigned)burst[2], (unsigned)burst[3],
           (int)i16le(burst), (int)i16le(&burst[2]),
           (unsigned)hi, (unsigned)lo);
    if (i16le(burst) == (int16_t)-32768)
        printf("HTS221 %s: RH RAW 0x8000; not a documented error code; "
               "RH remains INVALID\r\n", label);
    return true;
}

void sense_hts221_diagnose(bool one_shot) {
    if (!hts_bus) {
        printf("HTS221 DIAG: unavailable (initialization failed)\r\n");
        return;
    }

    uint8_t ctrl1 = 0, ctrl2 = 0, av_conf = 0, id = 0;
    if (!hts_diag_read(0x0Fu, &id) ||
        !hts_diag_read(0x20u, &ctrl1) ||
        !hts_diag_read(0x21u, &ctrl2) ||
        !hts_diag_read(0x10u, &av_conf)) {
        printf("HTS221 DIAG: control register READ_FAILED\r\n");
        return;
    }
    printf("HTS221 DIAG: ID=0x%02X CTRL1=0x%02X CTRL2=0x%02X "
           "heater=%u AV_CONF=0x%02X mode=%s\r\n",
           (unsigned)id, (unsigned)ctrl1, (unsigned)ctrl2,
           (unsigned)((ctrl2 >> 1u) & 1u), (unsigned)av_conf,
           one_shot ? "ONESHOT_REQUEST" : "READ_ONLY");

    hts_diag_print_cal();
    if (!one_shot) {
        (void)hts_diag_read_measurements("CONT");
        return;
    }

    // One-shot is opt-in, not called during normal boot or sampling.
    // This guard prevents overwriting unknown settings / heater state.
    if (id != 0xBCu || ctrl1 != 0x86u || ctrl2 != 0x00u) {
        printf("HTS221 ONESHOT: SKIPPED (unexpected CTRL/ID; "
               "requires ID=BC CTRL1=86 CTRL2=00)\r\n");
        return;
    }

    bool attempted_switch = false;
    bool converted = false;
    uint8_t status = 0;
    uint8_t discard[4] = {0};

    attempted_switch = true;
    if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x20u, 0x84u)) {
        printf("HTS221 ONESHOT: failed to enter one-shot mode\r\n");
        goto restore;
    }

    // Drain old 7 Hz samples so H_DA/T_DA cannot be mistaken for the
    // fresh one-shot conversion. BDU releases after the MSB reads.
    if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG),
                         discard, sizeof(discard)) ||
        !hts_diag_read(0x27u, &status)) {
        printf("HTS221 ONESHOT: failed to clear old sample status\r\n");
        goto restore;
    }
    if ((status & 0x03u) != 0u) {
        printf("HTS221 ONESHOT: SKIPPED (data-ready not clear: 0x%02X)\r\n",
               (unsigned)status);
        goto restore;
    }

    // CTRL_REG2.ONE_SHOT=1; BOOT and HEATER intentionally remain zero.
    if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, 0x01u)) {
        printf("HTS221 ONESHOT: trigger failed\r\n");
        goto restore;
    }

    for (unsigned retry = 0; retry < 60u; ++retry) {
        sleep_ms(10); // Max 600 ms; manual diagnostic, not steady-state path.
        if (!hts_diag_read(0x27u, &status)) {
            printf("HTS221 ONESHOT: status read failed\r\n");
            goto restore;
        }
        if ((status & 0x03u) == 0x03u) {
            converted = true;
            break;
        }
    }
    if (!converted) {
        printf("HTS221 ONESHOT: TIMEOUT status=0x%02X (H_DA=%u T_DA=%u)\r\n",
               (unsigned)status, (unsigned)((status >> 1u) & 1u),
               (unsigned)(status & 1u));
        goto restore;
    }
    (void)hts_diag_read_measurements("ONESHOT");

restore:
    // Restore previous continuous measurement settings even if the
    // conversion failed. CTRL2 ONE_SHOT self-clears on completion.
    if (attempted_switch) {
        bool restored2 = sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, 0x00u);
        bool restored1 = sensor_i2c_write(hts_bus, HTS_ADDR, 0x20u, ctrl1);
        if (!restored1 || !restored2) {
            printf("HTS221 ONESHOT: RESTORE_FAILED; HTS221 reads disabled "
                   "until reset\r\n");
            hts_bus = NULL;
            return;
        }
    }
    if (attempted_switch) {
        uint8_t verify_ctrl1 = 0, verify_ctrl2 = 0;
        if (!hts_diag_read(0x20u, &verify_ctrl1) ||
            !hts_diag_read(0x21u, &verify_ctrl2) ||
            verify_ctrl1 != ctrl1 || verify_ctrl2 != 0x00u) {
            printf("HTS221 ONESHOT: RESTORE_VERIFY_FAILED; "
                   "HTS221 reads disabled until reset\r\n");
            hts_bus = NULL;
            return;
        }
    }
    printf("HTS221 ONESHOT: finished; continuous mode %s (CTRL1=0x%02X)\r\n",
           attempted_switch ? "restored" : "unchanged", (unsigned)ctrl1);
}


/*
 * Compare the current HTS221 averaging setting against factory default.
 * ST DocID026333 Rev 5, Table 16:
 *   0x1B: temp 16 / RH 32 internal averages
 *   0x3F: temp 256 / RH 512 internal averages
 *
 * Manual USB-CDC command only. No BOOT, calibration, or HEATER writes.
 * Uses the existing 7 Hz continuous mode. One repeated 1Hz scheduling
 * delay during the manual test is expected.
 */
void sense_hts221_averaging_test(void) {
    if (!hts_bus) {
        printf("HTS221 AVGTEST: unavailable (initialization failed)\r\n");
        return;
    }

    uint8_t id = 0, ctrl1 = 0, ctrl2 = 0, original_avg = 0;
    if (!hts_diag_read(0x0Fu, &id) ||
        !hts_diag_read(0x20u, &ctrl1) ||
        !hts_diag_read(0x21u, &ctrl2) ||
        !hts_diag_read(0x10u, &original_avg)) {
        printf("HTS221 AVGTEST: control READ_FAILED\r\n");
        return;
    }
    printf("HTS221 AVGTEST: original AV_CONF=0x%02X "
           "ID=0x%02X CTRL1=0x%02X CTRL2=0x%02X\r\n",
           (unsigned)original_avg, (unsigned)id,
           (unsigned)ctrl1, (unsigned)ctrl2);
    if (id != 0xBCu || ctrl1 != 0x86u || ctrl2 != 0x00u ||
        (original_avg != 0x3Fu && original_avg != 0x1Bu)) {
        printf("HTS221 AVGTEST: SKIPPED (unexpected configuration)\r\n");
        return;
    }

    const uint8_t default_avg = 0x1Bu;
    const bool change_needed = original_avg != default_avg;
    bool write_attempted = false;
    bool capture_ok = true;
    uint8_t verify = 0u;

    if (change_needed) {
        // Mark the write as attempted before sending it: an I2C error
        // may occur after the sensor has accepted the new register value.
        write_attempted = true;
        if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x10u, default_avg) ||
            !hts_diag_read(0x10u, &verify) || verify != default_avg) {
            printf("HTS221 AVGTEST: default AV_CONF write/verify failed\r\n");
            goto restore;
        }
    }
    printf("HTS221 AVGTEST: testing AV_CONF=0x1B "
           "(T=16 averages, RH=32 averages)\r\n");

    for (unsigned i = 0; i < 8u; ++i) {
        // 7Hz output gives ~143ms between samples; 200ms normally
        // provides a new conversion. The STATUS flag is still checked.
        sleep_ms(200);
        uint8_t status = 0u, raw[4] = {0};
        if (!hts_diag_read(0x27u, &status)) {
            printf("HTS221 AVGTEST: #%u STATUS READ_FAILED\r\n", i + 1u);
            capture_ok = false;
            break;
        }
        if ((status & 0x03u) != 0x03u) {
            printf("HTS221 AVGTEST: #%u NOT_READY status=0x%02X\r\n",
                   i + 1u, (unsigned)status);
            continue;
        }
        if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                             (uint8_t)(0x28u | ST_BURST_FLAG),
                             raw, sizeof(raw))) {
            printf("HTS221 AVGTEST: #%u OUTPUT READ_FAILED\r\n", i + 1u);
            capture_ok = false;
            break;
        }
        printf("HTS221 AVGTEST: #%u status=0x%02X "
               "Hraw=%d Traw=%d bytes=%02X %02X %02X %02X\r\n",
               i + 1u, (unsigned)status,
               (int)i16le(raw), (int)i16le(&raw[2]),
               (unsigned)raw[0], (unsigned)raw[1],
               (unsigned)raw[2], (unsigned)raw[3]);
    }

restore:
    if (write_attempted) {
        // Restore the exact original AV_CONF even if a write or read
        // unexpectedly failed; then read back to establish its value.
        bool restored = sensor_i2c_write(hts_bus, HTS_ADDR,
                                         0x10u, original_avg) &&
                        hts_diag_read(0x10u, &verify) &&
                        verify == original_avg;
        if (!restored) {
            printf("HTS221 AVGTEST: RESTORE_FAILED; "
                   "HTS221 reads disabled until hardware reset\r\n");
            hts_bus = NULL;
            return;
        }
    }
    printf("HTS221 AVGTEST: %s; AV_CONF restored to 0x%02X\r\n",
           capture_ok ? "DONE" : "INCOMPLETE",
           (unsigned)original_avg);
}


/*
 * HTS221 complete documented-register dump, ST DocID026333 Rev 5,
 * register map section 6 / conversion section 8.
 *
 * Read-only I2C access. Skips undocumented address ranges rather than
 * pretending unknown reserved locations are part of a stable register API.
 * A read of 0x29 / 0x2B clears STATUS.H_DA / STATUS.T_DA, respectively;
 * this is the documented *read side effect*, not a register write.
 */
static void hts_dump_fixed_centi(const char *name, int32_t centi) {
    int64_t absvalue = centi < 0 ? -(int64_t)centi : (int64_t)centi;
    printf("%s=%s%ld.%02ld", name, centi < 0 ? "-" : "",
           (long)(absvalue / 100), (long)(absvalue % 100));
}

void sense_hts221_dump(void) {
    if (!hts_bus) {
        printf("HTS221 DUMP: unavailable (sensor not initialized)\r\n");
        return;
    }
    // Capture complete defined register map. CTRL_REG3 was not present
    // in the earlier hts diag command; it is included here.
    static const struct {
        uint8_t addr;
        const char *name;
    } controls[] = {
        {0x0Fu, "WHO_AM_I"},
        {0x10u, "AV_CONF"},
        {0x20u, "CTRL_REG1"},
        {0x21u, "CTRL_REG2"},
        {0x22u, "CTRL_REG3"}
    };
    uint8_t ctrl[sizeof(controls) / sizeof(controls[0])];
    uint8_t cal[16] = {0};
    uint8_t sample[4] = {0};
    uint8_t status_before = 0, status_after = 0;
    uint32_t started_ms = to_ms_since_boot(get_absolute_time());

    printf("HTS221 DUMP BEGIN addr=0x5F uptime_ms=%lu READ_ONLY\r\n",
           (unsigned long)started_ms);
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        if (!sensor_i2c_read(hts_bus, HTS_ADDR, controls[i].addr,
                             &ctrl[i], 1u)) {
            printf("HTS221 DUMP ERROR at 0x%02X (%s): I2C_READ_FAILED\r\n",
                   (unsigned)controls[i].addr, controls[i].name);
            printf("HTS221 DUMP END status=PARTIAL\r\n");
            return;
        }
        printf("HTS221 REG [0x%02X] %-13s = 0x%02X\r\n",
               (unsigned)controls[i].addr, controls[i].name,
               (unsigned)ctrl[i]);
    }

    // Single 16-byte factory-calibration snapshot. Every byte is dumped
    // with its register address, including reserved slots inside 30..3F.
    // Absolutely no writes are issued to the factory-calibration bank.
    if (!sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x30u | ST_BURST_FLAG),
                         cal, sizeof(cal))) {
        printf("HTS221 DUMP ERROR CALIB 0x30..0x3F: I2C_READ_FAILED\r\n");
        printf("HTS221 DUMP END status=PARTIAL\r\n");
        return;
    }
    for (unsigned row = 0; row < sizeof(cal); row += 4u) {
        printf("HTS221 CAL");
        for (unsigned i = row; i < row + 4u; ++i)
            printf(" [0x%02X]=%02X",
                   (unsigned)(0x30u + i), (unsigned)cal[i]);
        printf("\r\n");
    }

    // Keep STATUS and output data adjacent in time. The 4-byte burst
    // reads 0x28..0x2B consistently using the documented auto-increment
    // bit 7, and completes both BDU register pairs.
    if (!sensor_i2c_read(hts_bus, HTS_ADDR, 0x27u,
                         &status_before, 1u) ||
        !sensor_i2c_read(hts_bus, HTS_ADDR,
                         (uint8_t)(0x28u | ST_BURST_FLAG),
                         sample, sizeof(sample)) ||
        !sensor_i2c_read(hts_bus, HTS_ADDR, 0x27u,
                         &status_after, 1u)) {
        printf("HTS221 DUMP ERROR STATUS/DATA 0x27..0x2B: "
               "I2C_READ_FAILED\r\n");
        printf("HTS221 DUMP END status=PARTIAL\r\n");
        return;
    }

    printf("HTS221 REG [0x27] STATUS_BEFORE = 0x%02X "
           "(H_DA=%u T_DA=%u)\r\n",
           (unsigned)status_before,
           (unsigned)((status_before >> 1u) & 1u),
           (unsigned)(status_before & 1u));
    printf("HTS221 REG [0x28] HUMIDITY_OUT_L = 0x%02X\r\n",
           (unsigned)sample[0]);
    printf("HTS221 REG [0x29] HUMIDITY_OUT_H = 0x%02X\r\n",
           (unsigned)sample[1]);
    printf("HTS221 REG [0x2A] TEMP_OUT_L     = 0x%02X\r\n",
           (unsigned)sample[2]);
    printf("HTS221 REG [0x2B] TEMP_OUT_H     = 0x%02X\r\n",
           (unsigned)sample[3]);
    printf("HTS221 REG [0x27] STATUS_AFTER  = 0x%02X "
           "(high-byte reads clear data-ready flags)\r\n",
           (unsigned)status_after);

    int16_t h_raw = i16le(&sample[0]);
    int16_t t_raw = i16le(&sample[2]);
    uint8_t h0_x2 = cal[0], h1_x2 = cal[1];
    uint16_t t0_x8 = (uint16_t)(cal[2] | ((uint16_t)(cal[5] & 0x03u) << 8u));
    uint16_t t1_x8 = (uint16_t)(cal[3] | ((uint16_t)(cal[5] & 0x0Cu) << 6u));
    int16_t h0_raw = i16le(&cal[6]), h1_raw = i16le(&cal[10]);
    int16_t t0_raw = i16le(&cal[12]), t1_raw = i16le(&cal[14]);

    printf("HTS221 DECODE Hraw=0x%04X (%d) Traw=0x%04X (%d)\r\n",
           (unsigned)(uint16_t)h_raw, (int)h_raw,
           (unsigned)(uint16_t)t_raw, (int)t_raw);
    printf("HTS221 DECODE CAL H0x2=%u H1x2=%u H0raw=%d H1raw=%d "
           "T0x8=%u T1x8=%u T0raw=%d T1raw=%d\r\n",
           (unsigned)h0_x2, (unsigned)h1_x2,
           (int)h0_raw, (int)h1_raw,
           (unsigned)t0_x8, (unsigned)t1_x8,
           (int)t0_raw, (int)t1_raw);

    if (h0_raw == h1_raw || t0_raw == t1_raw) {
        printf("HTS221 DECODE: INVALID_FACTORY_CALIBRATION "
               "(zero interpolation denominator)\r\n");
        printf("HTS221 DUMP END status=CAL_INVALID\r\n");
        return;
    }
    int32_t rh_centi = (int32_t)h0_x2 * 50 +
        div_signed_rounded((int64_t)((int32_t)h_raw - h0_raw) *
                           ((int32_t)h1_x2 - h0_x2) * 50,
                           (int32_t)h1_raw - h0_raw);
    int32_t temp_centi = div_signed_rounded((int64_t)t0_x8 * 100, 8) +
        div_signed_rounded((int64_t)((int32_t)t_raw - t0_raw) *
                           ((int32_t)t1_x8 - t0_x8) * 100,
                           (int64_t)8 * ((int32_t)t1_raw - t0_raw));
    printf("HTS221 DECODE ");
    hts_dump_fixed_centi("T_C", temp_centi);
    printf(" ");
    hts_dump_fixed_centi("RH_percent_unclamped", rh_centi);
    printf(" RH_VALID=%s\r\n",
           (rh_centi >= 0 && rh_centi <= 10000 &&
            h_raw != (int16_t)-32768) ? "YES" : "NO");
    printf("HTS221 DECODE AV_CONF: T_AVG=%u RH_AVG=%u "
           "CTRL1_PD=%u BDU=%u ODR_BITS=%u HEATER=%u\r\n",
           (unsigned)(2u << ((ctrl[1] >> 3u) & 7u)),
           (unsigned)(4u << (ctrl[1] & 7u)),
           (unsigned)((ctrl[2] >> 7u) & 1u),
           (unsigned)((ctrl[2] >> 2u) & 1u),
           (unsigned)(ctrl[2] & 0x03u),
           (unsigned)((ctrl[3] >> 1u) & 1u));
    printf("HTS221 DUMP END status=OK "
           "(reserved 0x00..0x0E/0x11..0x1F/0x23..0x26/0x2C..0x2F "
           "not accessed)\r\n");
}


void sense_hts221_recovery_init(void) {
    hts_auto_phase = HTS_AUTO_DISABLED;
    hts_auto_have_raw = false;
    hts_auto_last_rh_valid = false;
    hts_auto_cycles_completed = 0u;
    if (!hts_bus) {
        printf("HTS221 AUTO: SKIPPED (not initialized)\r\n");
        return;
    }
    uint8_t id = 0u, ctrl1 = 0u, ctrl2 = 0u;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!sensor_i2c_read(hts_bus, HTS_ADDR, 0x0Fu, &id, 1u) ||
        !hts_auto_read_ctrl(&ctrl1, &ctrl2)) {
        printf("HTS221 AUTO: SKIPPED (device read failed)\r\n");
        return;
    }
    // A MCU-only reset may retain the sensor's heater state. Priority is
    // ALWAYS to switch it off. Never start cycles if control is unknown.
    if (ctrl2 & 0x02u) {
        printf("HTS221 AUTO: inherited HEATER=ON; forcing OFF first\r\n");
        if (!hts_auto_heater_off()) {
            hts_auto_enter_off_fault(now);
            return;
        }
        ctrl2 &= (uint8_t)~0x02u;
    }
    if (id != 0xBCu || ctrl1 != 0x86u || ctrl2 != 0x00u) {
        printf("HTS221 AUTO: SKIPPED ID=%02X CTRL1=%02X CTRL2=%02X "
               "(requires BC/86/00)\r\n",
               (unsigned)id, (unsigned)ctrl1, (unsigned)ctrl2);
        return;
    }
#if HTS_AUTO_ENABLE_HEATING == 0
    // Read-only scheduled logging: no more experimental heater cycles on
    // each reboot. All existing CLI diagnostics remain available.
    hts_auto_phase = HTS_AUTO_OBSERVE_ONLY;
    hts_auto_next_log_ms = now + HTS_AUTO_LOG_MS;
    printf("HTS221 AUTO: OBSERVE_ONLY every 5s; HEATER OFF verified; "
           "no automatic heating\r\n");
    return;
#else
    hts_auto_phase = HTS_AUTO_BASELINE;
    hts_auto_deadline_ms = now + HTS_AUTO_BASELINE_MS;
    hts_auto_next_log_ms = now + HTS_AUTO_LOG_MS;
    printf("HTS221 AUTO: ENABLED baseline=5s heat=10s cooldown=30s "
           "max_cycles=3 log=5s, starts automatically\r\n");
#endif
}

bool sense_hts221_recovery_busy(void) {
    return hts_auto_phase == HTS_AUTO_BASELINE ||
           hts_auto_phase == HTS_AUTO_HEATING ||
           hts_auto_phase == HTS_AUTO_COOLING ||
           hts_auto_phase == HTS_AUTO_OFF_FAULT;
}

bool sense_hts221_recovery_sample_blocked(void) {
    return hts_auto_phase == HTS_AUTO_HEATING ||
           hts_auto_phase == HTS_AUTO_COOLING ||
           hts_auto_phase == HTS_AUTO_OFF_FAULT;
}

void sense_hts221_recovery_stop(void) {
    if (hts_auto_phase == HTS_AUTO_DISABLED ||
        hts_auto_phase == HTS_AUTO_DONE) {
        printf("HTS221 AUTO: already stopped\r\n");
        return;
    }
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!hts_auto_heater_off()) {
        hts_auto_enter_off_fault(now);
        return;
    }
    // If heat was active, allow sensor to stabilize after heater-off.
    if (hts_auto_phase == HTS_AUTO_HEATING ||
        hts_auto_phase == HTS_AUTO_OFF_FAULT) {
        hts_auto_phase = HTS_AUTO_COOLING;
        hts_auto_deadline_ms = now + HTS_AUTO_COOL_MS;
        hts_auto_cycles_completed = HTS_AUTO_MAX_CYCLES; // no restart
        printf("HTS221 AUTO: ABORTED; HEATER OFF verified; "
               "cooldown 30s then DONE\r\n");
    } else {
        hts_auto_phase = HTS_AUTO_DONE;
        printf("HTS221 AUTO: STOPPED; HEATER OFF verified\r\n");
    }
}

void sense_hts221_recovery_poll(void) {
    if (!hts_bus || hts_auto_phase == HTS_AUTO_DISABLED) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (hts_auto_phase == HTS_AUTO_OBSERVE_ONLY) {
        if (hts_auto_reached(now, hts_auto_next_log_ms)) {
            hts_auto_next_log_ms = now + HTS_AUTO_LOG_MS;
            if (hts_auto_have_raw) {
                printf("HTS221 AUTO: uptime=%lums phase=OBSERVE_ONLY "
                       "Hraw=%d Traw=%d RH_VALID=%s HEATER_AUTO=DISABLED\r\n",
                       (unsigned long)now, (int)hts_auto_last_hraw,
                       (int)hts_auto_last_traw,
                       hts_auto_last_rh_valid ? "YES" : "NO");
            } else {
                printf("HTS221 AUTO: uptime=%lums phase=OBSERVE_ONLY "
                       "raw=NOT_YET_SAMPLED HEATER_AUTO=DISABLED\r\n",
                       (unsigned long)now);
            }
        }
        return;
    }

    // A 5s status print is deliberately independent from the 1Hz sensor
    // stream. No extra RH/T register read occurs while heater is ON.
    if (hts_auto_reached(now, hts_auto_next_log_ms)) {
        hts_auto_next_log_ms = now + HTS_AUTO_LOG_MS;
        unsigned log_cycle = hts_auto_cycles_completed;
        if (hts_auto_phase == HTS_AUTO_BASELINE ||
            hts_auto_phase == HTS_AUTO_HEATING ||
            hts_auto_phase == HTS_AUTO_OFF_FAULT) {
            if (log_cycle < HTS_AUTO_MAX_CYCLES) ++log_cycle;
        }
        const char *stage = "DONE";
        if (hts_auto_phase == HTS_AUTO_BASELINE) stage = "BASELINE";
        else if (hts_auto_phase == HTS_AUTO_HEATING) stage = "HEATING";
        else if (hts_auto_phase == HTS_AUTO_COOLING) stage = "COOLDOWN";
        else if (hts_auto_phase == HTS_AUTO_OFF_FAULT) stage = "OFF_FAULT";

        if (hts_auto_phase == HTS_AUTO_HEATING ||
            hts_auto_phase == HTS_AUTO_COOLING ||
            hts_auto_phase == HTS_AUTO_OFF_FAULT) {
            printf("HTS221 AUTO: uptime=%lums phase=%s cycle=%u/%u "
                   "HTS_T_RH=WITHHELD\r\n",
                   (unsigned long)now, stage,
                   log_cycle, HTS_AUTO_MAX_CYCLES);
        } else if (hts_auto_have_raw) {
            printf("HTS221 AUTO: uptime=%lums phase=%s cycle=%u/%u "
                   "last_Hraw=%d last_Traw=%d "
                   "(same latest 1Hz sample)\r\n",
                   (unsigned long)now, stage,
                   log_cycle, HTS_AUTO_MAX_CYCLES,
                   (int)hts_auto_last_hraw, (int)hts_auto_last_traw);
        } else {
            printf("HTS221 AUTO: uptime=%lums phase=%s "
                   "raw=NOT_YET_SAMPLED\r\n",
                   (unsigned long)now, stage);
        }
    }

    if (hts_auto_phase == HTS_AUTO_OFF_FAULT) {
        if (hts_auto_reached(now, hts_auto_next_off_retry_ms)) {
            hts_auto_next_off_retry_ms = now + HTS_AUTO_OFF_RETRY_MS;
            if (hts_auto_heater_off()) {
                printf("HTS221 AUTO: HEATER OFF now VERIFIED; "
                       "30s cooldown, no further heating\r\n");
                hts_auto_cycles_completed = HTS_AUTO_MAX_CYCLES;
                hts_auto_phase = HTS_AUTO_COOLING;
                hts_auto_deadline_ms = now + HTS_AUTO_COOL_MS;
            }
        }
        return;
    }

    if (!hts_auto_reached(now, hts_auto_deadline_ms)) return;

    if (hts_auto_phase == HTS_AUTO_BASELINE) {
        // Do not heat a working sensor, and stop cycling early if
        // RH has returned to the valid 0..100% range after cooldown.
        if (hts_auto_have_raw && hts_auto_last_rh_valid) {
            printf("HTS221 AUTO: RH has returned within valid range; "
                   "no further heating. HEATER OFF\r\n");
            hts_auto_phase = HTS_AUTO_DONE;
            return;
        }
        uint8_t ctrl1 = 0, ctrl2 = 0, id = 0;
        if (!sensor_i2c_read(hts_bus, HTS_ADDR, 0x0Fu, &id, 1u) ||
            !hts_auto_read_ctrl(&ctrl1, &ctrl2) ||
            id != 0xBCu || ctrl1 != 0x86u || ctrl2 != 0x00u) {
            printf("HTS221 AUTO: ABORT (unsafe/unexpected sensor state); "
                   "no further heater cycles\r\n");
            hts_auto_phase = HTS_AUTO_DONE;
            return;
        }
        // The only non-diagnostic write: CTRL_REG2.HEATER=1.
        // Hardware does not auto-stop: polling OFF at deadline is essential.
        if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, 0x02u) ||
            !sensor_i2c_read(hts_bus, HTS_ADDR, 0x21u, &ctrl2, 1u) ||
            ctrl2 != 0x02u) {
            printf("HTS221 AUTO: HEATER ON failed/unconfirmed\r\n");
            if (!hts_auto_heater_off()) hts_auto_enter_off_fault(now);
            else hts_auto_phase = HTS_AUTO_DONE;
            return;
        }
        hts_auto_phase = HTS_AUTO_HEATING;
        hts_auto_deadline_ms = now + HTS_AUTO_HEAT_MS;
        printf("HTS221 AUTO: HEATER ON verified cycle=%u/%u "
               "for 10s; HTS221 T/RH readings suppressed\r\n",
               hts_auto_cycles_completed + 1u, HTS_AUTO_MAX_CYCLES);
        return;
    }
    if (hts_auto_phase == HTS_AUTO_HEATING) {
        if (!hts_auto_heater_off()) {
            hts_auto_enter_off_fault(now);
            return;
        }
        ++hts_auto_cycles_completed;
        hts_auto_phase = HTS_AUTO_COOLING;
        hts_auto_deadline_ms = now + HTS_AUTO_COOL_MS;
        printf("HTS221 AUTO: HEATER OFF verified; "
               "cycle=%u/%u, cooldown=30s\r\n",
               hts_auto_cycles_completed, HTS_AUTO_MAX_CYCLES);
        return;
    }
    if (hts_auto_phase == HTS_AUTO_COOLING) {
        if (hts_auto_cycles_completed >= HTS_AUTO_MAX_CYCLES) {
            hts_auto_phase = HTS_AUTO_DONE;
            printf("HTS221 AUTO: COMPLETED 3 cycles; "
                   "HEATER OFF; logging every 5s continues\r\n");
        } else {
            hts_auto_phase = HTS_AUTO_BASELINE;
            hts_auto_deadline_ms = now + HTS_AUTO_BASELINE_MS;
            printf("HTS221 AUTO: cooldown complete; "
                   "5s baseline before next bounded heater cycle\r\n");
        }
    }
}


/*
 * One-time boot verification of HTS221 features not previously confirmed
 * on the RP2350. Independent H_DA/T_DA one-shot readiness, readback and
 * factory trim BOOT reload. Runs before the first 1Hz acquisition, never
 * in an interrupt. Writes ONLY CTRL1/CTRL2/AV_CONF; no heater / NVM write.
 *
 * Mechanism PASS does NOT imply valid RH. Numerical validity is reported
 * separately. A failed register-restore readback quarantines HTS221.
 */
static bool hts_verify_read_burst(uint8_t addr, uint8_t *bytes, size_t length) {
    return sensor_i2c_read(hts_bus, HTS_ADDR,
                           (uint8_t)(addr | ST_BURST_FLAG), bytes, length);
}

static bool hts_verify_continuous(const char *label) {
    uint8_t status = 0u, raw[4] = {0};
    // Discard an old frame first: STATUS ready alone does not prove
    // the data appeared *after* the BOOT or ONE_SHOT test boundary.
    if (!hts_verify_read_burst(0x28u, raw, sizeof(raw))) {
        printf("HTS221 VERIFY %s: STALE_DRAIN_FAILED\r\n", label);
        return false;
    }
    for (unsigned n = 0; n < 15u; ++n) {
        if (!hts_diag_read(0x27u, &status)) {
            printf("HTS221 VERIFY %s: STATUS_READ_FAILED\r\n", label);
            return false;
        }
        if ((status & 0x03u) == 0x03u) break;
        sleep_ms(40u);
    }
    if ((status & 0x03u) != 0x03u) {
        printf("HTS221 VERIFY %s: NOT_READY status=0x%02X\r\n",
               label, (unsigned)status);
        return false;
    }
    if (!hts_verify_read_burst(0x28u, raw, sizeof(raw))) {
        printf("HTS221 VERIFY %s: BURST_READ_FAILED\r\n", label);
        return false;
    }
    int16_t rh_raw = i16le(raw);
    int16_t t_raw = i16le(&raw[2]);
    int32_t rh_centi = (int32_t)hts_cal.h0_x2 * 50 +
        div_signed_rounded((int64_t)((int32_t)rh_raw - hts_cal.h0_out) *
                           ((int32_t)hts_cal.h1_x2 - hts_cal.h0_x2) * 50,
                           (int32_t)hts_cal.h1_out - hts_cal.h0_out);
    printf("HTS221 VERIFY %s: READY=YES status=0x%02X "
           "Hraw=%d (0x%04X) Traw=%d RH_unclamped_centi=%ld "
           "RH_VALID=%s\r\n", label, (unsigned)status,
           (int)rh_raw, (unsigned)(uint16_t)rh_raw,
           (int)t_raw, (long)rh_centi,
           (rh_centi >= 0 && rh_centi <= 10000 &&
            rh_raw != (int16_t)-32768) ? "YES" : "NO");
    return true;
}

void sense_hts221_verify_once(void) {
    if (!hts_bus) {
        printf("HTS221 VERIFY: SKIP (sensor not initialized)\r\n");
        return;
    }

    uint8_t id = 0u, initial_ctrl1 = 0u, initial_ctrl2 = 0u;
    uint8_t initial_avg = 0u, cal_before[16] = {0}, cal_after[16] = {0};
    if (!hts_diag_read(0x0Fu, &id) ||
        !hts_diag_read(0x20u, &initial_ctrl1) ||
        !hts_diag_read(0x21u, &initial_ctrl2) ||
        !hts_diag_read(0x10u, &initial_avg) ||
        !hts_verify_read_burst(0x30u, cal_before, sizeof(cal_before))) {
        printf("HTS221 VERIFY: FAIL initial register read\r\n");
        return;
    }

    printf("HTS221 VERIFY BEGIN ID=%02X CTRL1=%02X CTRL2=%02X "
           "AV_CONF=%02X heater=%u\r\n", (unsigned)id,
           (unsigned)initial_ctrl1, (unsigned)initial_ctrl2,
           (unsigned)initial_avg, (unsigned)((initial_ctrl2 >> 1u) & 1u));

    if (id != 0xBCu || initial_ctrl1 != 0x86u ||
        initial_ctrl2 != 0x00u) {
        printf("HTS221 VERIFY: SKIP writes (unexpected configuration)\r\n");
        return;
    }

    (void)hts_verify_continuous("PRE_BOOT");

    /* CTRL2.BOOT=1 reloads internal factory trimming, not the
     * external calibration register bank. A single bounded reload
     * is enough; the bit must clear autonomously. */
    bool boot_started = true; // even a failed I2C write may take effect
    bool boot_cleared = false;
    uint8_t ctrl2 = 0u, ctrl1 = 0u, avg = 0u;
    if (sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, 0x80u)) {
        for (unsigned n = 0; n < 100u; ++n) {
            sleep_ms(10u);
            if (!hts_diag_read(0x21u, &ctrl2)) break;
            if ((ctrl2 & 0x80u) == 0u) {
                boot_cleared = true;
                break;
            }
        }
    }
    printf("HTS221 VERIFY BOOT: bit_self_cleared=%s CTRL2=0x%02X\r\n",
           boot_cleared ? "YES" : "NO", (unsigned)ctrl2);

    // Always attempt to restore/verify exact pre-test configuration,
    // including after a failed BOOT write (I2C ACK can be lost).
    bool boot_restore = !boot_started ||
        (sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, initial_ctrl2) &&
         hts_diag_read(0x20u, &ctrl1) &&
         hts_diag_read(0x21u, &ctrl2) &&
         hts_diag_read(0x10u, &avg) &&
         ctrl1 == initial_ctrl1 && ctrl2 == initial_ctrl2 &&
         avg == initial_avg);
    if (!boot_restore) {
        printf("HTS221 VERIFY: BOOT RESTORE_FAILED; "
               "HTS221 reads quarantined until reset\r\n");
        hts_bus = NULL;
        return;
    }
    if (hts_verify_read_burst(0x30u, cal_after, sizeof(cal_after))) {
        bool same = true;
        for (unsigned i = 0; i < sizeof(cal_before); ++i)
            if (cal_before[i] != cal_after[i]) same = false;
        printf("HTS221 VERIFY BOOT: calibration_bytes_unchanged=%s "
               "(this is NOT an internal trim readback)\r\n",
               same ? "YES" : "NO");
    } else {
        printf("HTS221 VERIFY BOOT: calibration_readback_FAILED\r\n");
    }
    (void)hts_verify_continuous("POST_BOOT");

    /* Arduino cross-check: switch into ONE_SHOT mode and sample H_DA
     * separately from T_DA instead of requiring simultaneous readiness.
     * Use factory-default 0x1B averaging ONLY during this controlled
     * test, then restore the exact initial averaging value. */
    bool touched_avg = false, touched_ctrl1 = false, touched_ctrl2 = false;
    bool h_ready = false, t_ready = false, trigger_ok = false;
    bool oneshot_ok = false, cleanup_ok = true;
    uint8_t raw_h[2] = {0}, raw_t[2] = {0}, status = 0u;
    unsigned h_ms = 0u, t_ms = 0u;
    touched_avg = true;
    if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x10u, 0x1Bu) ||
        !hts_diag_read(0x10u, &avg) || avg != 0x1Bu) {
        printf("HTS221 VERIFY ONESHOT: AV_CONF_SET_FAILED\r\n");
        goto restore_oneshot;
    }
    touched_ctrl1 = true;
    if (!sensor_i2c_write(hts_bus, HTS_ADDR, 0x20u, 0x84u) ||
        !hts_diag_read(0x20u, &ctrl1) || ctrl1 != 0x84u) {
        printf("HTS221 VERIFY ONESHOT: CTRL1_SET_FAILED\r\n");
        goto restore_oneshot;
    }
    // Clear stale data-ready by reading both HIGH output registers.
    {
        uint8_t stale[4] = {0};
        if (!hts_verify_read_burst(0x28u, stale, sizeof(stale)) ||
            !hts_diag_read(0x27u, &status) ||
            (status & 0x03u) != 0u) {
            printf("HTS221 VERIFY ONESHOT: OLD_READY_NOT_CLEARED "
                   "status=0x%02X\r\n", (unsigned)status);
            goto restore_oneshot;
        }
    }
    touched_ctrl2 = true;
    trigger_ok = sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, 0x01u);
    if (!trigger_ok) {
        printf("HTS221 VERIFY ONESHOT: TRIGGER_WRITE_FAILED\r\n");
        goto restore_oneshot;
    }
    for (unsigned n = 1u; n <= 150u; ++n) {
        sleep_ms(20u);
        if (!hts_diag_read(0x27u, &status)) {
            printf("HTS221 VERIFY ONESHOT: STATUS_READ_FAILED\r\n");
            break;
        }
        if (!h_ready && (status & 0x02u)) {
            if (!hts_verify_read_burst(0x28u, raw_h, sizeof(raw_h))) {
                printf("HTS221 VERIFY ONESHOT: H_READ_FAILED\r\n");
                break;
            }
            h_ready = true;
            h_ms = n * 20u;
        }
        if (!t_ready && (status & 0x01u)) {
            if (!hts_verify_read_burst(0x2Au, raw_t, sizeof(raw_t))) {
                printf("HTS221 VERIFY ONESHOT: T_READ_FAILED\r\n");
                break;
            }
            t_ready = true;
            t_ms = n * 20u;
        }
        if (h_ready && t_ready) break;
    }
    printf("HTS221 VERIFY ONESHOT: H_DA=%u Hraw=%d H_ms=%u "
           "T_DA=%u Traw=%d T_ms=%u last_status=%02X\r\n",
           (unsigned)h_ready, h_ready ? (int)i16le(raw_h) : 0, h_ms,
           (unsigned)t_ready, t_ready ? (int)i16le(raw_t) : 0, t_ms,
           (unsigned)status);
    // A humidity-only completion is not an RH failure of the One-shot
    // mechanism; it is recorded separately from T readiness.
    if (!hts_diag_read(0x21u, &ctrl2))
        printf("HTS221 VERIFY ONESHOT: CTRL2_READ_FAILED\r\n");
    else
        printf("HTS221 VERIFY ONESHOT: ONE_SHOT_bit_cleared=%s "
               "CTRL2=0x%02X\r\n",
               (ctrl2 & 1u) == 0u ? "YES" : "NO", (unsigned)ctrl2);
    oneshot_ok = h_ready;

restore_oneshot:
    // Keep going after each failed restoration write so all affected
    // registers get their own restore attempt. Verify ALL three.
    if (touched_ctrl2 &&
        !sensor_i2c_write(hts_bus, HTS_ADDR, 0x21u, initial_ctrl2))
        cleanup_ok = false;
    if (touched_ctrl1 &&
        !sensor_i2c_write(hts_bus, HTS_ADDR, 0x20u, initial_ctrl1))
        cleanup_ok = false;
    if (touched_avg &&
        !sensor_i2c_write(hts_bus, HTS_ADDR, 0x10u, initial_avg))
        cleanup_ok = false;
    if (!hts_diag_read(0x20u, &ctrl1) ||
        !hts_diag_read(0x21u, &ctrl2) ||
        !hts_diag_read(0x10u, &avg) ||
        ctrl1 != initial_ctrl1 || ctrl2 != initial_ctrl2 ||
        avg != initial_avg) cleanup_ok = false;

    if (!cleanup_ok) {
        printf("HTS221 VERIFY: ONESHOT RESTORE_FAILED; "
               "HTS221 reads quarantined until reset\r\n");
        hts_bus = NULL;
        return;
    }
    printf("HTS221 VERIFY ONESHOT: restore=PASS "
           "CTRL1=%02X CTRL2=%02X AV_CONF=%02X "
           "H_READY=%s T_READY=%s\r\n",
           (unsigned)ctrl1, (unsigned)ctrl2, (unsigned)avg,
           h_ready ? "YES" : "NO", t_ready ? "YES" : "NO");
    (void)hts_verify_continuous("POST_ONESHOT");
    printf("HTS221 VERIFY END: BOOT_CLEAR=%s ONESHOT_H=%s "
           "ONESHOT_T=%s (see Hraw/RH validity separately)\r\n",
           boot_cleared ? "YES" : "NO",
           oneshot_ok ? "YES" : "NO", t_ready ? "YES" : "NO");
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
