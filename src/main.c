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
#define I2C_TIMEOUT_US 20000u

static void scan_i2c(void) {
    unsigned found = 0;
    printf("I2C scan (7-bit addresses):\r\n");

    for (uint8_t address = 0x08u; address <= 0x77u; ++address) {
        uint8_t value = 0;
        // Read-only probe; a response indicates an ACK but may consume one
        // byte on sensors with stateful read protocols.
        int rc = i2c_read_timeout_us(
            SENSOR_I2C, address, &value, 1, false, I2C_TIMEOUT_US);
        if (rc == 1) {
            printf("  ACK at 0x%02X\r\n", address);
            ++found;
        }
    }
    printf("Scan complete: %u responders\r\n", found);
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

int main(void) {
    stdio_init_all();
    i2c_init(SENSOR_I2C, SENSOR_I2C_HZ);
    gpio_set_function(SENSOR_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SENSOR_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SENSOR_SDA_PIN);
    gpio_pull_up(SENSOR_SCL_PIN);

    // Allow a USB CDC terminal to attach, without blocking forever.
    sleep_ms(3000);
    printf("\r\nRP2350 Environment Monitor - bring-up\r\n");
    printf("Board: waveshare_rp2350_pizero; I2C1 GPIO2/GPIO3\r\n");

    while (true) {
        scan_i2c();
        check_bme280();
        sleep_ms(5000);
    }
}
