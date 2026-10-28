#include "sgp40.h"
#include "sensor_i2c_util.h"
#include "sensirion_gas_index_algorithm.h"
#include "pico/time.h"

#define SGP_ADDR 0x59u
static i2c_inst_t *sgp_bus;
static GasIndexAlgorithmParams algorithm;

static uint16_t clamp_ticks(int64_t ticks) {
    if (ticks < 0) return 0;
    if (ticks > 65535) return 65535;
    return (uint16_t)ticks;
}
static void set_word(uint8_t *p, uint16_t word) {
    p[0] = (uint8_t)(word >> 8);
    p[1] = (uint8_t)(word & 0xFFu);
    p[2] = sensor_crc8(p, 2);
}

bool sgp40_init(i2c_inst_t *bus) {
    sgp_bus = NULL;
    // Identity command with three independent CRC-protected words.
    const uint8_t cmd[2] = {0x36u, 0x82u};
    uint8_t serial[9] = {0};
    if (!sensor_i2c_command(bus, SGP_ADDR, cmd, 2)) return false;
    sleep_ms(2);
    if (!sensor_i2c_receive(bus, SGP_ADDR, serial, 9)) return false;
    for (size_t i = 0; i < 9u; i += 3u)
        if (serial[i + 2] != sensor_crc8(&serial[i], 2)) return false;
    GasIndexAlgorithm_init(&algorithm, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);
    sgp_bus = bus;
    return true;
}

bool sgp40_read(int32_t temperature_centi_c,
                uint32_t humidity_centi_rh,
                sgp40_sample_t *s) {
    if (!sgp_bus || !s) return false;

    // RH tick = RH[%] * 65535 / 100;
    // T tick = (T[degC] + 45) * 65535 / 175.
    uint16_t rh = clamp_ticks(((int64_t)humidity_centi_rh * 65535 + 5000) / 10000);
    uint16_t tc = clamp_ticks(((int64_t)temperature_centi_c + 4500) * 65535 / 17500);

    uint8_t cmd[8] = {0x26u, 0x0Fu};
    set_word(&cmd[2], rh);
    set_word(&cmd[5], tc);
    if (!sensor_i2c_command(sgp_bus, SGP_ADDR, cmd, sizeof(cmd))) return false;
    sleep_ms(30);
    uint8_t raw[3] = {0};
    if (!sensor_i2c_receive(sgp_bus, SGP_ADDR, raw, 3) ||
        raw[2] != sensor_crc8(raw, 2)) return false;
    s->sraw_voc = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    GasIndexAlgorithm_process(&algorithm, (int32_t)s->sraw_voc, &s->voc_index);
    // Index is zero during the first ~45 seconds. It is NOT a VOC ppm reading.
    s->index_ready = s->voc_index > 0;
    return true;
}
