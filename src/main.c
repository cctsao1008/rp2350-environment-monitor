#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define SENSOR_I2C i2c1
#define SENSOR_SDA_PIN 2u
#define SENSOR_SCL_PIN 3u
#define SENSOR_I2C_HZ 100000u
#define BME280_ADDR 0x76u
#define BME280_CHIP_ID_REG 0xD0u
#define SGP40_ADDR 0x59u
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

int main(void) {
    stdio_init_all();
    i2c_init(SENSOR_I2C, SENSOR_I2C_HZ);
    gpio_set_function(SENSOR_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SENSOR_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SENSOR_SDA_PIN);
    gpio_pull_up(SENSOR_SCL_PIN);

    sleep_ms(3000);
    printf("\r\nRP2350 Environment Monitor - bring-up\r\n");
    printf("Board: waveshare_rp2350_pizero; I2C1 GPIO2/GPIO3\r\n");

    while (true) {
        scan_i2c();
        check_bme280();
        check_sgp40();
        sleep_ms(5000);
    }
}
