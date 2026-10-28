#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "sensor_manager.h"

#define SENSOR_I2C i2c1
#define SENSOR_SDA_PIN 2u
#define SENSOR_SCL_PIN 3u
#define SENSOR_I2C_HZ 100000u
#define BME280_ADDR 0x76u
#define BME280_CHIP_ID_REG 0xD0u
#define SGP40_ADDR 0x59u
#define TSL2591_ADDR 0x29u
#define TSL2591_DEVICE_ID_CMD 0xB2u // COMMAND (0xA0) + ID register (0x12)
#define LTR390_ADDR 0x53u
#define LTR390_PART_ID_REG 0x06u
#define ICM20948_ADDR 0x68u
#define ICM20948_BANK_SEL_REG 0x7Fu
#define ICM20948_WHO_AM_I_REG 0x00u
#define I2C_TIMEOUT_US 20000u

// Sensirion SGP40 CRC-8: polynomial 0x31, initial value 0xFF.
// One CRC byte follows each two data bytes.
static uint8_t sensirion_crc8(const uint8_t *data, size_t length) {
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x80u)
                ? (uint8_t)((crc << 1u) ^ 0x31u)
                : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

static void scan_i2c(void) {
    unsigned responders = 0;
    printf("I2C read-probe scan (7-bit addresses):\r\n");
    printf("Note: a read-probe is NOT a reliable presence test for command-based sensors.\r\n");

    for (uint8_t address = 0x08u; address <= 0x77u; ++address) {
        uint8_t value = 0;
        // A one-byte read can be NACKed by a present sensor that requires
        // a command first (e.g. SGP40). It can also consume pending data.
        int rc = i2c_read_timeout_us(
            SENSOR_I2C, address, &value, 1, false, I2C_TIMEOUT_US);
        if (rc == 1) {
            printf("  READ ACK at 0x%02X\r\n", address);
            ++responders;
        }
    }
    printf("Read-probe responders: %u (not a sensor count)\r\n", responders);
}

static void check_bme280(void) {
    const uint8_t reg = BME280_CHIP_ID_REG;
    uint8_t chip_id = 0;

    int wrote = i2c_write_timeout_us(
        SENSOR_I2C, BME280_ADDR, &reg, 1, true, I2C_TIMEOUT_US);
    if (wrote != 1) {
        printf("BME280: register select failed (%d)\r\n", wrote);
        return;
    }

    int read = i2c_read_timeout_us(
        SENSOR_I2C, BME280_ADDR, &chip_id, 1, false, I2C_TIMEOUT_US);
    if (read != 1) {
        printf("BME280: chip ID read failed (%d)\r\n", read);
        return;
    }

    printf("BME280: chip ID 0x%02X (%s)\r\n",
           chip_id, chip_id == 0x60u ? "PASS" : "UNEXPECTED");
}


// Read a single-byte identity register via command/register-pointer write
// followed by a repeated-START read. This does not configure measurements.
static bool read_identity_reg(uint8_t address, uint8_t reg, uint8_t *value) {
    int rc = i2c_write_timeout_us(
        SENSOR_I2C, address, &reg, 1, true, I2C_TIMEOUT_US);
    if (rc != 1) {
        printf("I2C 0x%02X: select 0x%02X failed (%d)\r\n", address, reg, rc);
        return false;
    }
    rc = i2c_read_timeout_us(
        SENSOR_I2C, address, value, 1, false, I2C_TIMEOUT_US);
    if (rc != 1) {
        printf("I2C 0x%02X: read 0x%02X failed (%d)\r\n", address, reg, rc);
        return false;
    }
    return true;
}

static void check_tsl2591(void) {
    // ams TSL2591: command byte 0xB2 selects read-only ID register 0x12.
    uint8_t id = 0;
    if (!read_identity_reg(TSL2591_ADDR, TSL2591_DEVICE_ID_CMD, &id)) {
        printf("TSL2591: ID read FAIL\r\n");
        return;
    }
    printf("TSL2591: ID 0x%02X (%s)\r\n",
           id, id == 0x50u ? "PASS" : "UNEXPECTED");
}

static void check_ltr390(void) {
    // Bits [7:4] are the fixed 0xB part number; [3:0] is revision.
    uint8_t id = 0;
    if (!read_identity_reg(LTR390_ADDR, LTR390_PART_ID_REG, &id)) {
        printf("LTR390: PART_ID read FAIL\r\n");
        return;
    }
    printf("LTR390: PART_ID 0x%02X (part 0x%X, rev 0x%X) (%s)\r\n",
           id, (unsigned)(id >> 4), (unsigned)(id & 0x0Fu),
           (id >> 4) == 0x0Bu ? "PASS" : "UNEXPECTED");
}

static void check_icm20948(void) {
    // Select user bank 0 before reading WHO_AM_I; do not assume the
    // previous firmware or driver left the active bank at zero.
    const uint8_t select_bank0[] = {ICM20948_BANK_SEL_REG, 0x00u};
    int rc = i2c_write_timeout_us(
        SENSOR_I2C, ICM20948_ADDR, select_bank0,
        sizeof(select_bank0), false, I2C_TIMEOUT_US);
    if (rc != (int)sizeof(select_bank0)) {
        printf("ICM20948: bank 0 select failed (%d)\r\n", rc);
        return;
    }
    uint8_t id = 0;
    if (!read_identity_reg(ICM20948_ADDR, ICM20948_WHO_AM_I_REG, &id)) {
        printf("ICM20948: WHO_AM_I read FAIL\r\n");
        return;
    }
    printf("ICM20948: WHO_AM_I 0x%02X (%s)\r\n",
           id, id == 0xEAu ? "PASS" : "UNEXPECTED");
}

static void check_sgp40(void) {
    // SGP40 Get Serial Number (0x3682) -> 3 x (2 data bytes + CRC).
    // The sensor normally NACKs an arbitrary read without a command.
    static const uint8_t command[] = {0x36u, 0x82u};
    uint8_t response[9] = {0};

    int wrote = i2c_write_timeout_us(
        SENSOR_I2C, SGP40_ADDR, command, sizeof(command), false, I2C_TIMEOUT_US);
    if (wrote != (int)sizeof(command)) {
        printf("SGP40: serial command failed (%d)\r\n", wrote);
        return;
    }

    sleep_ms(2);

    int read = i2c_read_timeout_us(
        SENSOR_I2C, SGP40_ADDR, response, sizeof(response), false, I2C_TIMEOUT_US);
    if (read != (int)sizeof(response)) {
        printf("SGP40: serial response failed (%d)\r\n", read);
        return;
    }

    for (unsigned i = 0; i < 3u; ++i) {
        const uint8_t *word = &response[i * 3u];
        if (sensirion_crc8(word, 2u) != word[2]) {
            printf("SGP40: serial CRC FAIL at word %u\r\n", i);
            return;
        }
    }

    printf("SGP40: serial CRC PASS, serial ");
    for (unsigned i = 0; i < 3u; ++i) {
        printf("%02X%02X", response[i * 3u], response[i * 3u + 1u]);
    }
    printf("\r\n");
}

// Format fixed-point results without requiring printf floating-point support.
static void print_bme280(const bme280_reading_t *sample) {
    int32_t centi = sample->temperature_centi_c;
    const char *sign = centi < 0 ? "-" : "";
    uint32_t mag = (uint32_t)(centi < 0 ? -centi : centi);
    uint32_t rh = sample->humidity_centi_percent;
    uint32_t pa = sample->pressure_pa;

    printf("BME280: temp=%s%lu.%02lu C, humidity=%lu.%02lu %%RH, "
           "pressure=%lu.%02lu hPa\r\n",
           sign, (unsigned long)(mag / 100u), (unsigned long)(mag % 100u),
           (unsigned long)(rh / 100u), (unsigned long)(rh % 100u),
           (unsigned long)(pa / 100u), (unsigned long)(pa % 100u));
}

int main(void) {
    stdio_init_all();
    i2c_init(SENSOR_I2C, SENSOR_I2C_HZ);
    gpio_set_function(SENSOR_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SENSOR_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SENSOR_SDA_PIN);
    gpio_pull_up(SENSOR_SCL_PIN);

    sleep_ms(3000);
    printf("\r\nRP2350 Environment Monitor - P1 BME280 acquisition\r\n");
    printf("Board: waveshare_rp2350_pizero; I2C1 GPIO2/GPIO3\r\n");

    // One-time bus diagnostics and device identification.
    scan_i2c();
    check_bme280();
    check_sgp40();
    check_tsl2591();
    check_ltr390();
    check_icm20948();

    int8_t rc = sensor_manager_init(SENSOR_I2C);
    if (rc != 0) {
        printf("Sensor Manager: BME280 init FAIL (%d)\r\n", rc);
        while (true) {
            sleep_ms(5000);
        }
    }
    printf("Sensor Manager: BME280 ready (forced mode, 1 Hz)\r\n");

    absolute_time_t next = get_absolute_time();
    while (true) {
        sensor_snapshot_t snapshot = {0};
        rc = sensor_manager_sample(&snapshot);
        if (rc == 0) {
            print_bme280(&snapshot.bme280);
        } else {
            printf("BME280: sample FAIL (%d)\r\n", rc);
        }
        next = delayed_by_ms(next, 1000u);
        sleep_until(next);
    }
}
