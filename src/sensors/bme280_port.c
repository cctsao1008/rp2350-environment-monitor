#include "bme280_port.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "bme280.h"
#include "pico/time.h"

#define BME280_I2C_ADDRESS 0x76u
#define I2C_TIMEOUT_US 20000u
#define MAX_WRITE_PAYLOAD 32u

typedef struct {
    i2c_inst_t *bus;
    uint8_t address;
} i2c_context_t;

static i2c_context_t context;
static struct bme280_dev device;
static struct bme280_settings settings;
static uint32_t measurement_delay_us;
static bool initialized;

static int8_t read_registers(
    uint8_t reg, uint8_t *data, uint32_t length, void *intf_ptr) {
    i2c_context_t *ctx = (i2c_context_t *)intf_ptr;
    if (ctx == NULL || data == NULL || length == 0u || length > 255u) {
        return BME280_E_COMM_FAIL;
    }

    // Repeated START: register address write, followed by data read.
    int rc = i2c_write_timeout_us(
        ctx->bus, ctx->address, &reg, 1, true, I2C_TIMEOUT_US);
    if (rc != 1) {
        return BME280_E_COMM_FAIL;
    }
    rc = i2c_read_timeout_us(
        ctx->bus, ctx->address, data, (size_t)length, false, I2C_TIMEOUT_US);
    return rc == (int)length ? BME280_INTF_RET_SUCCESS : BME280_E_COMM_FAIL;
}

static int8_t write_registers(
    uint8_t reg, const uint8_t *data, uint32_t length, void *intf_ptr) {
    i2c_context_t *ctx = (i2c_context_t *)intf_ptr;
    if (ctx == NULL || data == NULL || length == 0u || length > MAX_WRITE_PAYLOAD) {
        return BME280_E_COMM_FAIL;
    }

    uint8_t tx[MAX_WRITE_PAYLOAD + 1u];
    tx[0] = reg;
    memcpy(&tx[1], data, (size_t)length);
    int rc = i2c_write_timeout_us(
        ctx->bus, ctx->address, tx, length + 1u, false, I2C_TIMEOUT_US);
    return rc == (int)(length + 1u) ? BME280_INTF_RET_SUCCESS : BME280_E_COMM_FAIL;
}

static void delay_us_callback(uint32_t period, void *intf_ptr) {
    (void)intf_ptr;
    sleep_us(period);
}

int8_t bme280_port_init(i2c_inst_t *bus) {
    initialized = false;
    if (bus == NULL) {
        return BME280_E_NULL_PTR;
    }

    context.bus = bus;
    context.address = BME280_I2C_ADDRESS;
    memset(&device, 0, sizeof(device));
    memset(&settings, 0, sizeof(settings));

    device.intf = BME280_I2C_INTF;
    device.intf_ptr = &context;
    device.read = read_registers;
    device.write = write_registers;
    device.delay_us = delay_us_callback;

    int8_t rc = bme280_init(&device);  // Chip ID + factory calibration data
    if (rc != BME280_OK) {
        return rc;
    }

    // Forced mode is well-suited to low-rate (1 Hz) environmental sampling.
    // OSR T=2x, P=4x, H=1x; no IIR filter for first-stage validation.
    settings.osr_t = BME280_OVERSAMPLING_2X;
    settings.osr_p = BME280_OVERSAMPLING_4X;
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

    rc = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &device);
    if (rc != BME280_OK) {
        return rc;
    }
    rc = bme280_cal_meas_delay(&measurement_delay_us, &settings);
    if (rc == BME280_OK) {
        initialized = true;
    }
    return rc;
}

int8_t bme280_port_read(bme280_reading_t *reading) {
    if (reading == NULL) {
        return BME280_E_NULL_PTR;
    }
    if (!initialized) {
        return BME280_E_COMM_FAIL;
    }

    int8_t rc = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &device);
    if (rc != BME280_OK) {
        return rc;
    }
    sleep_us(measurement_delay_us + 1000u);

    struct bme280_data result = {0};
    rc = bme280_get_sensor_data(BME280_ALL, &result, &device);
    if (rc != BME280_OK) {
        return rc;
    }

    // Bosch's BME280_DOUBLE_ENABLE compensation returns degrees C, Pa, %RH.
    // Convert once to explicitly documented fixed-point API units.
    double scaled_temp = result.temperature * 100.0;
    reading->temperature_centi_c = (int32_t)(
        scaled_temp + (scaled_temp < 0.0 ? -0.5 : 0.5));
    reading->humidity_centi_percent = (uint32_t)(result.humidity * 100.0 + 0.5);
    reading->pressure_pa = (uint32_t)(result.pressure + 0.5);
    return BME280_OK;
}
