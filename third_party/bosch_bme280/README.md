# Bosch BME280 SensorAPI (vendored)

The source files `bme280.c`, `bme280.h`, and `bme280_defs.h` are
**unmodified** copies from:

- Upstream: https://github.com/boschsensortec/BME280_SensorAPI
- Pinned upstream commit: `c90d419492e26dd95586598a794e65eb2760753a`
- License: BSD-3-Clause (see `LICENSE` in this directory)

This source is tracked in the firmware repository so **normal builds never
fetch code from the network**. Platform I2C callbacks and sensor sampling policy
are maintained separately in `src/sensors/bme280_port.c`.

For future updates, replace these upstream files as a reviewed, explicit change;
do not automatically track upstream master during build.
