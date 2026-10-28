# P1: complete sensor data acquisition (five sensors)

**Status:** INITIAL ALL-SENSOR HARDWARE DATA-ACQUISITION PASS (2026-10-28). Nine consecutive 1 Hz records with six successful initializations (five main sensors plus the AK09916 magnetometer). Light accuracy/UV stimulus, SGP40 VOC Index after warmup, movement response and endurance tests remain open.

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
| AK09916 | magnetic field XYZ (µT) | Bypass access and 3-axis output confirmed on hardware; no hard/soft iron calibration or heading |

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

## Hardware measurement evidence (2026-10-28)

Captured user USB CDC output from the integrated firmware:

```text
INIT BME280=OK TSL2591=OK LTR390=OK SGP40=OK ICM20948=OK AK09916(mag)=OK
[sample=1 uptime=3477ms]
BME280  T=26.37C RH=56.25% P=1009.20hPa
TSL2591 CH0=1966 CH1_IR=315 visible=1651 lux=226.27 (estimate)
LTR390  UV=0 UVI=0.00 ALS=3048 lux=609.60 (estimates)
SGP40   SRAW_VOC=65535 VOC_Index=WARMING_UP (about 45s; not ppm)
ICM20948 ACCEL[g] x=0.050 -0.029 1.015 GYRO[dps] x=-0.595 -2.534 -1.878
AK09916  MAG[uT] -6.90 12.75 34.65 (uncalibrated)
[sample=9 uptime=11477ms]
BME280  T=26.37C RH=56.21% P=1009.14hPa
TSL2591 CH0=1967 CH1_IR=316 visible=1651 lux=226.16 (estimate)
LTR390  UV=0 UVI=0.00 ALS=3049 lux=609.80 (estimates)
SGP40   SRAW_VOC=24663 VOC_Index=WARMING_UP (about 45s; not ppm)
ICM20948 ACCEL[g] x=0.052 -0.009 1.010 GYRO[dps] x=-1.450 0.840 0.336
AK09916  MAG[uT] -6.00 14.40 35.55 (uncalibrated)
```

- All six `INIT` states were `OK`; repeated readings were visible for all configured sensor paths.
- `uptime_ms` increments by exactly 1000 ms between nine samples (3477 to 11477 ms); no `ERROR` or sampling-overrun text was present in the excerpt.
- BME280 readings are stable around 26.37-26.38 °C, 56.19-56.25 %RH, 1009.14-1009.21 hPa.
- TSL2591 reads about 226 Lux, while LTR390 reports about 609 Lux under the reported setup (about 2.7x difference). Both are currently **estimated**; do not force them to agree without controlled light-source/reference measurements.
- LTR390 UVS was 0 across this indoor-like sample; this is plausible, but its UV response is **not** validated by observing zero.
- SGP40 first raw SRAW of 65535 (16-bit maximum), then values progress to 24663 by sample 9; CRC/communication path active. The first raw sample may be a startup transient; check steady operation. VOC Index remained in the specified initial algorithm blackout, so post-blackout processing is **not yet validated**.
- ICM20948 Z acceleration is near 1 g and AK09916 returns three nonzero magnetometer axes; controlled rotation, magnetic orientation, and calibration checks remain open.

## Required on-target checks

- [x] This integrated firmware built and ran sufficiently to produce USB CDC telemetry; exact build transcript for this revision was not supplied. Offline/no-download behavior still needs separate qualification.
- [ ] ELF program + verify + reboot using PICOBOOT (not MSC copy).
- [x] All five sensors and AK09916 magnetometer report INIT OK.
- [x] BME280 T/RH/P stable for nine records in integrated firmware.
- [x] TSL2591 raw channels and estimated lux output present.
- [ ] TSL2591 darkness/lighting response, saturation and reference lux validation.
- [x] LTR390 ALS and UV raw fields present with successful sampled status (UV = 0 in provided conditions).
- [ ] LTR390 UV stimulus / data freshness under changing light / reference comparison.
- [x] SGP40 returns SRAW values with command/CRC path active and startup warming indicator.
- [ ] Post-45s VOC Index output, response to controlled VOC changes and algorithm stability.
- [x] ICM20948 static ~1g acceleration, raw gyro and AK09916 3-axis magnetic readings.
- [ ] Gyro zero-bias/rotation test, magnetic response and calibration.
- [ ] Ten-minute stable sample sequence with error counts and average sample period.
- [ ] Offline second incremental build; automatic runtime PICOBOOT update verified.
- [ ] Reference-instrument comparison (measurement *accuracy*, separate from function).

Capture output and keep failed components as explicit FAIL/PENDING rather than reporting complete hardware validation.
