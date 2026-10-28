# Validation record

Status: **P0 SENSOR IDENTIFICATION PASSED (5/5)**. Other infrastructure and functional validation remains open.

## Evidence: 2026-10-28 (user-provided Windows/USB CDC logs)

- [x] Host dependencies resolved: Git, CMake, Ninja, Arm GCC 15.2, picotool, pioasm, Pico SDK pinned at `079c6f3902...`.
- [x] CMake configure completed and Ninja linked `environment_monitor.elf` (92/92 steps).
- [x] Programming via **PICOBOOT + picotool**, `-NoForce`: flash 100%, verify 100% OK, reboot requested; no MSC file copy.
- [x] Application running with USB CDC output (I2C scan and BME280 output observed).
- [x] BME280 register `0xD0` returned `0x60` (PASS).
- [ ] Offline second incremental build / disconnected network test.
- [ ] Automatic runtime reset with `picotool -f` (without BOOT button).
- [ ] Disable-MSC-on-runtime-reset behavior confirmed.
- [x] SGP40 serial command response 9 bytes and three CRC-8 checks PASS; serial `000007C138AD`.
- [x] TSL2591 device ID from address `0x29`: `0x50` (PASS).
- [x] LTR390 PART_ID from address `0x53`: `0xB2` (part `0xB`, revision `0x2`) (PASS).
- [x] ICM20948 WHO_AM_I from address `0x68`: `0xEA` (PASS).
- [x] Consecutive repeated identity scans show matching values with no errors in the supplied sample.
- [x] BME280 compensated temperature, relative humidity, and pressure output functional PASS (33 consecutive USB CDC readings on 2026-10-28).
- [ ] BME280 absolute measurement accuracy and calibration verified against a reference.

### First read-probe scan

```text
ACK at 0x28
ACK at 0x29
ACK at 0x53
ACK at 0x68
ACK at 0x76
Scan complete: 5 responders
BME280: chip ID 0x60 (PASS)
```

Expected sensor addresses: 0x29 (TSL25911), 0x53 (LTR390), 0x59 (SGP40), 0x68 (ICM20948), 0x76 (BME280).

**Resolved:** 0x28 is the documented **secondary read-only I2C address of TSL2591** (primary 0x29), not a sixth physical device. Reference: [ams OSRAM TSL2591 datasheet, page 13](https://look.ams-osram.com/m/c901de8e97608f8/original/TSL2591-DS000338.pdf). 0x59 did not respond to an *uncommanded one-byte read*; this is an unsuitable SGP40 presence probe. A generic read-probe count must not be interpreted as the number of confirmed sensors.

### Identity verification results (2026-10-28)

Captured USB CDC log (repeated cycles contained the same values):

```text
Read-probe responders: 5 (not a sensor count)
BME280: chip ID 0x60 (PASS)
SGP40: serial CRC PASS, serial 000007C138AD
TSL2591: ID 0x50 (PASS)
LTR390: PART_ID 0xB2 (part 0xB, rev 0x2) (PASS)
ICM20948: WHO_AM_I 0xEA (PASS)
```

**PASS scope:** All five sensor identities successfully read at their intended I2C addresses. `0x28` is TSL2591's secondary read-only address. SGP40's `0x59` address is verified via explicit command + CRC, not generic read scanning.

**Not implied by these results:** Validated physical measurements, calibration, accuracy, long-duration stability, offline incremental build, or auto-reset/MSC enumeration behavior.

## P1 BME280 acquisition: first on-target measurements (2026-10-28)

User-reported 33 sequential lines of compensated BME280 data:

| Metric | Minimum | Maximum | Mean |
| --- | ---: | ---: | ---: |
| Temperature (°C) | 25.47 | 25.52 | 25.50 |
| Relative humidity (%RH) | 62.94 | 64.08 | 63.60 |
| Pressure (hPa) | 1009.07 | 1009.20 | 1009.12 |

No I2C/data errors appear in this captured sample. This is an initial **functional** validation only; keep accuracy, environmental stimulus-response, long-duration reliability and offline/no-MSC behavior open.

## P1 all-sensor firmware (source integration, not yet validated on this commit)

- [ ] TSL2591 compensated Lux output and saturation handling on hardware.
- [ ] LTR390 switched UV/ALS measurement, UVI/Lux on hardware.
- [ ] SGP40 RH/T-compensated SRAW and steady VOC Index after warmup on hardware.
- [ ] ICM20948 accel/gyro data and AK09916 magnetometer on hardware.
- [ ] Full integrated 1 Hz sampling and USB CDC error reporting on hardware.

Details: [P1 all-sensor validation plan](p1-all-sensors.md).

## Future P1 / P2

- [x] BME280 compensated-data acquisition: initial on-target functional test PASS (33 successive readings). See [P1 BME280 results](p1-bme280.md).
- [ ] Other sensor measurement drivers, calibration and data validity.
- [ ] VOC Index with temperature/humidity compensation.
- [ ] Timestamped USB CDC telemetry.
- [ ] microSD data logging.

Do not report PASS without corresponding captured hardware evidence.
