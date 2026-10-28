# P1: complete sensor data acquisition (five sensors)

**Status:** Firmware source integration completed. **Not yet compiled or verified on the user's RP2350-PiZero**. On-target results from the previous BME280-only firmware remain valid, but do not qualify this new revision.

## Architecture

```text
RP2350-PiZero / Pico SDK 2.3.1
    I2C1 (GPIO2 SDA, GPIO3 SCL, 100 kHz)
        BME280 0x76 -> Bosch SensorAPI / forced mode / T, RH, P
        TSL2591 0x29 -> 100 ms, 25x gain / full, IR, visible, estimated lux
        LTR390  0x53 -> UV + ALS alternating / UVI, estimated lux
        SGP40   0x59 -> compensated SRAW_VOC / Sensirion VOC Index
        ICM20948 0x68 -> XYZ accel, XYZ gyro
            AK09916 0x0C -> XYZ magnetometer via ICM20948 bypass
    Sensor Manager -> 1 Hz USB CDC output (sensor status + measurement)
```

The generic read-probe is a **one-time startup diagnostic only**. Initialization and samples are independent; one sensor failure should not stop the other available sensor data. No RTOS, no Wi-Fi, no extra hardware components.

## Measurement and status rules

| Device | Output | Notes |
| --- | --- | --- |
| BME280 | °C, %RH, hPa | Bosch vendor compensation, forced-mode sample; physical accuracy unverified |
| TSL2591 | CH0, IR, estimated visible counts and Lux | 100ms / 25x fixed gain; saturation reported explicitly. Lux is indicative, not calibrated |
| LTR390 | 24-bit UVS / ALS counts, estimated UVI and Lux | 18-bit resolution, 3x gain; switching UV/ALS modes between readings. UVI derived from datasheet counts-per-UVI reference |
| SGP40 | SRAW_VOC ticks, VOC Index | RH/T compensation from **fresh valid** BME280 readings; sensor command/result CRC; Sensirion algorithm 1 Hz, index suppressed during ~45s startup |
| ICM20948 | acceleration XYZ (g), gyroscope XYZ (°/s) | ±4g, ±500 °/s configured; raw coordinate axes, no sensor fusion |
| AK09916 | magnetic field XYZ (µT) | Through 0x0C via ICM20948 bypass; may report UNAVAILABLE until proven on target; no hard/soft iron calibration or heading |

A VOC Index is **not CO₂ concentration or ppm**. A UV Index estimated from a low-cost UV sensor is **not a calibrated UV exposure safety measurement**.

## Sampling

- Target one combined record per 1 second, with `sample` count and `uptime_ms`.
- A measurement cycle performs forced BME280 conversion, reads TSL2591, switches LTR390 UV/ALS mode (100ms integration), measures SGP40 raw (~30ms), and reads IMU.
- If a cycle exceeds one second, log an overrun warning; do not silently pretend the cadence was exact.
- I²C errors, invalid LTR data, UV/IR saturation, VOC warmup, and missing magnetometer are surfaced. No silently fabricated measurements.
- Vendor sources are **vendored and pinned**, so offline build never runs a sensor-library installer.

## Source references

- Bosch SensorAPI: `third_party/bosch_bme280` (BSD-3-Clause, pinned commit documented inside)
- Sensirion Gas Index Algorithm: `third_party/sensirion_gas_index` (BSD-3-Clause, pinned commit documented inside)
- TSL2591 register/gain/lux approximation: ams OSRAM datasheet and Adafruit TSL2591 reference implementation
- LTR390 register/mode/UVI conversion: Lite-On LTR-390UV-01 datasheet
- SGP40 measurement + RH/T ticks + CRC: Sensirion SGP40 datasheet
- ICM20948/AK09916 bank, bypass, scales: TDK InvenSense and AKM datasheets

## Required on-target checks

- [ ] Full build / link using the existing shared host tools (no downloads).
- [ ] ELF program + verify + reboot using PICOBOOT (not MSC copy).
- [ ] Init statuses for all five sensors, including magnetometer AK09916.
- [ ] BME280 T/RH/P stable (already passed on older revision; reverify).
- [ ] TSL2591: darkness/lighting reaction, saturated-state behavior.
- [ ] LTR390: ALS and UV both produce fresh data, check DRDY.
- [ ] SGP40: SRAW CRC, RH/T compensation, 45s initialization, steady VOC Index.
- [ ] ICM20948: static gravity axis, rotation response, 3-axis magnetic field.
- [ ] Ten-minute stable sample sequence with error counts and average sample period.
- [ ] Offline second incremental build; automatic runtime PICOBOOT update verified.
- [ ] Reference-instrument comparison (measurement *accuracy*, separate from function).

Capture output and keep failed components as explicit FAIL/PENDING rather than reporting complete hardware validation.
