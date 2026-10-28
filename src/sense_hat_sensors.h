#ifndef SENSE_HAT_SENSORS_H
#define SENSE_HAT_SENSORS_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

// Values in fixed-point engineering units; no heading or temperature
// compensation between two HATs is implied.
typedef struct {
    int32_t temp_centi_c;
    uint32_t humidity_centi_rh; // clamped display only; see humidity_valid
    int32_t humidity_unclamped_centi_rh;
    int16_t humidity_raw;
    bool humidity_valid; // false if factory interpolation outside 0..100 %RH
} sense_hts221_sample_t;

typedef struct {
    int32_t temp_centi_c;
    int32_t pressure_centi_hpa;
} sense_lps25h_sample_t;

typedef struct {
    int32_t accel_mg[3];
    int32_t gyro_mdps[3];
    int32_t mag_centi_ut[3];
    bool mag_valid;
} sense_lsm9ds1_sample_t;

// One-shot, read-only preinit register snapshot. Must run BEFORE sensor_manager_init.
void sense_hts221_preinit_snapshot(i2c_inst_t *bus);

// Independent init paths: failures on one IC do not block the others.
bool sense_hts221_init(i2c_inst_t *bus);
bool sense_hts221_read(sense_hts221_sample_t *out);
// USB CDC interactive fault isolation; one_shot=true temporarily changes mode.
void sense_hts221_diagnose(bool one_shot);
// Manual controlled comparison against factory default AV_CONF=0x1B.
void sense_hts221_averaging_test(void);
// Read-only dump of all documented registers, raw samples, factory calibration.
void sense_hts221_dump(void);
// One-time automated firmware checks: factory BOOT + H/T independent ONE_SHOT.
void sense_hts221_verify_once(void);
void sense_hts221_recovery_init(void);
void sense_hts221_recovery_poll(void);
void sense_hts221_recovery_stop(void);
bool sense_hts221_recovery_busy(void);
bool sense_hts221_recovery_sample_blocked(void);
bool sense_lps25h_init(i2c_inst_t *bus);
bool sense_lps25h_read(sense_lps25h_sample_t *out);
bool sense_lsm9ds1_init(i2c_inst_t *bus);
bool sense_lsm9ds1_read(sense_lsm9ds1_sample_t *out);

#endif
