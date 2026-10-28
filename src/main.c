#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "sensor_manager.h"
#include "sense_hat_led.h"
#include "sense_hat_probe.h"
#include "sense_hat_marquee.h"
#include "sense_hat_console.h"

#define SENSOR_BUS i2c1
#define SDA_PIN 2u
#define SCL_PIN 3u
#define SENSOR_BUS_HZ 100000u

// Temporary debug output policy: only HTS221 data on USB CDC.
// All sensors STILL initialize and sample at 1Hz (including BME280
// compensation for SGP40); no acquisition, I2C or control path changes.
// Change to 0 to restore the previous full telemetry stream.
#define HTS221_ONLY_OUTPUT 1

#if !HTS221_ONLY_OUTPUT
// Generic read probing does not count physical devices. SGP40 requires a
// command before read, and TSL2591 has an additional read-only 0x28 alias.
static void scan_i2c_once(void) {
    unsigned responders = 0;
    printf("I2C initial read-probe (diagnostic only):");
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        uint8_t ignored = 0;
        if (i2c_read_timeout_us(SENSOR_BUS, addr, &ignored, 1,
                                false, 30000u) == 1) {
            printf(" %02X", addr);
            ++responders;
        }
    }
    printf(" (%u responders; not a device count)\r\n", responders);
}
#endif // !HTS221_ONLY_OUTPUT

static void print_fixed(int32_t centi) {
    uint32_t absolute = (uint32_t)(centi < 0 ? -(int64_t)centi : centi);
    printf("%s%lu.%02lu", centi < 0 ? "-" : "",
           (unsigned long)(absolute / 100),
           (unsigned long)(absolute % 100));
}
#if !HTS221_ONLY_OUTPUT
static void print_thousandths(int32_t milli) {
    uint32_t absolute = (uint32_t)(milli < 0 ? -(int64_t)milli : milli);
    printf("%s%lu.%03lu", milli < 0 ? "-" : "",
           (unsigned long)(absolute / 1000),
           (unsigned long)(absolute % 1000));
}
#endif // !HTS221_ONLY_OUTPUT

// The HTS221 data format is shared between HTS-only and all-sensor modes.
static void output_hts221(const sensor_snapshot_t *s) {
    // Optional Raspberry Pi Sense HAT V1 measurements.
    // Values use the ST factory calibration where applicable; they are
    // printed separately, never silently averaged with the other HAT.
    if (s->hts221_ok) {
        printf("HTS221   T=");
        print_fixed(s->hts221.temp_centi_c);
        printf("C RH=");
        if (s->hts221.humidity_valid) {
            print_fixed((int32_t)s->hts221.humidity_centi_rh);
            printf("%%\r\n");
        } else {
            printf("INVALID (clipped=");
            print_fixed((int32_t)s->hts221.humidity_centi_rh);
            printf("%% raw=%d interp=", (int)s->hts221.humidity_raw);
            print_fixed(s->hts221.humidity_unclamped_centi_rh);
            printf("%%)\r\n");
        }
    } else {
        if (sense_hts221_recovery_sample_blocked()) {
            printf("HTS221   HEATER / COOLDOWN - T and RH withheld\r\n");
        } else {
            printf("HTS221   NOT_READY / ERROR / NOT_INSTALLED\r\n");
        }
    }

}

static void output(const sensor_snapshot_t *s) {
    printf("[sample=%lu uptime=%lums]\r\n",
           (unsigned long)s->sequence, (unsigned long)s->uptime_ms);
#if HTS221_ONLY_OUTPUT
    output_hts221(s);
#else

    if (s->bme280_ok) {
        printf("BME280  T=");
        print_fixed(s->bme280.temperature_centi_c);
        printf("C RH=");
        print_fixed((int32_t)s->bme280.humidity_centi_percent);
        printf("%% P=");
        print_fixed((int32_t)(s->bme280.pressure_pa)); // Pa / 100 = hPa
        printf("hPa\r\n");
    } else {
        printf("BME280  ERROR (SGP40 humidity compensation unavailable)\r\n");
    }

    if (s->tsl2591_ok) {
        printf("TSL2591 CH0=%u CH1_IR=%u visible=%u",
               (unsigned)s->tsl2591.full_count,
               (unsigned)s->tsl2591.ir_count,
               (unsigned)s->tsl2591.visible_count);
        if (s->tsl2591.saturated) {
            printf(" lux=SATURATED (reduce gain)\r\n");
        } else {
            printf(" lux=");
            print_fixed((int32_t)s->tsl2591.lux_centi);
            printf(" (estimate)\r\n");
        }
    } else {
        printf("TSL2591 ERROR\r\n");
    }

    if (s->ltr390_ok) {
        printf("LTR390  UV=%lu UVI=",
               (unsigned long)s->ltr390.uvs_counts);
        print_fixed((int32_t)s->ltr390.uvi_centi);
        printf(" ALS=%lu lux=",
               (unsigned long)s->ltr390.als_counts);
        print_fixed((int32_t)s->ltr390.lux_centi);
        printf(" (estimates)\r\n");
    } else {
        printf("LTR390  ERROR / DATA_NOT_READY\r\n");
    }

    if (s->sgp40_ok) {
        printf("SGP40   SRAW_VOC=%u VOC_Index=",
               (unsigned)s->sgp40.sraw_voc);
        if (s->sgp40.index_ready) {
            printf("%ld\r\n", (long)s->sgp40.voc_index);
        } else {
            printf("WARMING_UP (about 45s; not ppm)\r\n");
        }
    } else {
        printf("SGP40   ERROR / waiting for valid BME280 compensation\r\n");
    }

    if (s->icm20948_ok) {
        printf("ICM20948 ACCEL[g] x=");
        for (unsigned i = 0; i < 3; ++i) {
            if (i) printf(" ");
            print_thousandths(s->icm20948.accel_mg[i]);
        }
        printf(" GYRO[dps] x=");
        for (unsigned i = 0; i < 3; ++i) {
            if (i) printf(" ");
            print_thousandths(s->icm20948.gyro_mdps[i]);
        }
        printf("\r\nAK09916  MAG[uT] ");
        if (s->icm20948.mag_valid) {
            for (unsigned i = 0; i < 3; ++i) {
                if (i) printf(" ");
                print_fixed(s->icm20948.mag_centi_ut[i]);
            }
            printf(" (uncalibrated)\r\n");
        } else {
            printf(s->icm20948.mag_overflow ?
                   "OVERFLOW\r\n" : "UNAVAILABLE / NOT_READY\r\n");
        }
    } else {
        printf("ICM20948 ERROR\r\n");
    }

    output_hts221(s);

    if (s->lps25h_ok) {
        printf("LPS25H   T=");
        print_fixed(s->lps25h.temp_centi_c);
        printf("C P=");
        print_fixed(s->lps25h.pressure_centi_hpa);
        printf("hPa\r\n");
    } else {
        printf("LPS25H   NOT_READY / ERROR / NOT_INSTALLED\r\n");
    }

    if (s->lsm9ds1_ok) {
        printf("LSM9DS1  ACCEL[g] x=");
        for (unsigned i = 0; i < 3; ++i) {
            if (i) printf(" ");
            print_thousandths(s->lsm9ds1.accel_mg[i]);
        }
        printf(" GYRO[dps] x=");
        for (unsigned i = 0; i < 3; ++i) {
            if (i) printf(" ");
            print_thousandths(s->lsm9ds1.gyro_mdps[i]);
        }
        printf("\r\nLSM9DS1  MAG[uT] ");
        if (s->lsm9ds1.mag_valid) {
            for (unsigned i = 0; i < 3; ++i) {
                if (i) printf(" ");
                print_fixed(s->lsm9ds1.mag_centi_ut[i]);
            }
            printf(" (uncalibrated)\r\n");
        } else {
            printf("NOT_READY / ERROR\r\n");
        }
    } else {
        printf("LSM9DS1  NOT_READY / ERROR / NOT_INSTALLED\r\n");
    }
#endif // HTS221_ONLY_OUTPUT
}

int main(void) {
    stdio_init_all();
    i2c_init(SENSOR_BUS, SENSOR_BUS_HZ);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    // The ATtiny88 can initialize later than RP2350 on cold power-on,
    // or retain an old LED frame when only RP2350 is reset. Clear as
    // early as possible and several more times over the existing
    // 3-second USB CDC connect interval. Startup display default: OFF.
    bool sense_hat_led_off = sense_hat_led_clear_startup(SENSOR_BUS);
    sense_hat_marquee_init(SENSOR_BUS, sense_hat_led_off);
    printf("\r\nRP2350 Environment Monitor - all sensor acquisition\r\n");
    printf("Board: waveshare_rp2350_pizero, I2C1 GPIO2/3, 100kHz\r\n");
    printf("Sense HAT V1 LED matrix: %s\r\n",
           sense_hat_led_off ? "OFF (framebuffer cleared)" :
           "not detected / clear failed");
#if !HTS221_ONLY_OUTPUT
    scan_i2c_once();
    // Read-only identity diagnostics (otherwise omit other sensor output).
    sense_hat_probe_identify(SENSOR_BUS);
#endif

    sensor_presence_t present = {0};
    sensor_manager_init(SENSOR_BUS, &present);
    // Observe-only HTS221 monitoring (no automatic heater cycles).
    sense_hts221_recovery_init();
#if HTS221_ONLY_OUTPUT
    printf("Output mode: HTS221_ONLY (other sensor telemetry hidden, acquisition intact)\r\n");
    printf("INIT HTS221=%s\r\n", present.hts221 ? "OK" : "FAIL");
#else
    printf("INIT BME280=%s TSL2591=%s LTR390=%s SGP40=%s "
           "ICM20948=%s AK09916(mag)=%s\r\n",
           present.bme280 ? "OK" : "FAIL",
           present.tsl2591 ? "OK" : "FAIL",
           present.ltr390 ? "OK" : "FAIL",
           present.sgp40 ? "OK" : "FAIL",
           present.icm20948 ? "OK" : "FAIL",
           present.ak09916 ? "OK" : "FAIL");
    printf("Sense HAT V1 INIT HTS221=%s LPS25H=%s LSM9DS1=%s\r\n",
           present.hts221 ? "OK" : "FAIL",
           present.lps25h ? "OK" : "FAIL",
           present.lsm9ds1 ? "OK" : "FAIL");
    if (!present.ak09916) {
        printf("WARN: Magnetometer bypass not available; 6-axis IMU may still work.\r\n");
    }
#endif
    printf("LED controls: type 'led demo', 'led TEXT', 'led b 1..4' or 'led off'\r\n");
    printf("HTS221 tests: 'hts diag' (read only), 'hts oneshot' (restore 7Hz after test)\r\n");
    printf("HTS221: 'hts avgtest' tests default averaging and restores original\r\n");
    printf("HTS221: 'hts dump' dumps defined registers and raw/calibration data\r\n");
    printf("HTS221 AUTO: 5s raw logs, automatic heating disabled\r\n");
    absolute_time_t next = get_absolute_time();
    while (true) {
        sensor_snapshot_t reading;
        sensor_manager_sample(&reading);
        output(&reading);

        // Absolute 1 Hz cadence: do not let a coarse 5 ms polling sleep
        // manufacture a 2..5 ms deadline miss on every single sample.
        // Do not use I2C in an IRQ; marquee and console share this loop.
        next = delayed_by_ms(next, 1000);
        while (true) {
            int64_t remaining_us = absolute_time_diff_us(
                get_absolute_time(), next);
            if (remaining_us <= 0) break;

            // Leave the last few milliseconds for precise SDK sleep_until;
            // animation work (~192 frame bytes at 100 kHz) may take several ms.
            if (remaining_us <= 5000) {
                sleep_until(next);
                break;
            }
            sense_hat_console_poll();
            sense_hts221_recovery_poll();
            sense_hat_marquee_poll();
            sleep_ms(1);
        }
        const int64_t late_us = absolute_time_diff_us(
            next, get_absolute_time());
        // A normal scheduler boundary can incur tens/hundreds of us of
        // jitter. Only log actionable lateness (> 3ms), with its magnitude.
        if (late_us > 3000) {
            printf("WARN: sampling deadline late by %lld us\r\n",
                   (long long)late_us);
            next = get_absolute_time(); // resync only for a real overrun
        }
    }
}
