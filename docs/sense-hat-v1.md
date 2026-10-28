# Raspberry Pi Sense HAT V1 — acquisition and dual-HAT validation

## Status

- **2026-10-28 ID PASS**: ATtiny88 LED matrix cleared on real hardware;
  LSM9DS1 accel/gyro `0x6A/0x68`, LSM9DS1 magnetometer `0x1C/0x3D`,
  LPS25H `0x5C/0xBD` and HTS221 `0x5F/0xBC` all confirmed.
- **New firmware acquisition driver: SOURCE IMPLEMENTED / MEASUREMENT PENDING**.
  Never infer physical measurement PASS from the preceding ID log.

## Drivers and configuration

| Device | Address | Setup | 1 Hz USB CDC output |
| --- | --- | --- | --- |
| HTS221 | 0x5F | 7 Hz continuous, BDU; factory `0x30..0x3F` coefficients | calibrated T °C, RH % |
| LPS25H | 0x5C | 7 Hz continuous, BDU | pressure hPa (raw / 4096), die temperature °C |
| LSM9DS1 A/G | 0x6A | 119 Hz, accel ±4g, gyro ±500 dps, BDU/auto-increment | XYZ accel g, gyro dps |
| LSM9DS1 M | 0x1C | 20 Hz continuous, 4 gauss, BDU | XYZ magnetic field µT |

All Sense HAT sensor status fields are independent. If the Sense HAT is not
installed, the Environment HAT should continue normally.

The **acquisition output cadence is 1 Hz**, even though each IC is configured
for a faster internal data rate. The latter prevents repeated/stale raw samples.
**This is not a 100 Hz AHRS-ready implementation.** A separate high-rate path,
timestamps, coordinate-frame transforms, calibration and synchronized sampling
must be implemented and verified before performing cross-IMU fusion.

Sensor output is not fused or averaged. The Sense HAT's matrix is intentionally
kept OFF to reduce light contamination and self-heating; board proximity and
self-heating remain potential sources of temperature differences.

## Hardware acceptance

- [ ] Sense HAT INIT `HTS221=OK LPS25H=OK LSM9DS1=OK`.
- [ ] Sense HAT HTS221 °C / %RH plausible and responsive.
- [ ] Sense HAT LPS25H pressure hPa plausible, near BME280 under static conditions.
- [ ] Sense HAT LSM9DS1 accel magnitude near 1g at rest; gyro and mag response to rotation.
- [ ] Existing HAT `BME280`, `TSL2591`, `LTR390`, `SGP40`, `ICM20948`, `AK09916` still work.
- [ ] Nine-axis orientation alignment and magnetometer calibration (future).
- [ ] Ten-minute stability and USB CDC collection; temperature rise and LED OFF verification.

## References

- ST [HTS221 datasheet](https://www.st.com/resource/en/datasheet/hts221.pdf)
- ST [LPS25H datasheet](https://www.st.com/resource/en/datasheet/lps25h.pdf)
- ST [LSM9DS1 datasheet](https://www.st.com/resource/en/datasheet/lsm9ds1.pdf)
- [Arduino reference LSM9DS1 initialization](https://github.com/arduino-libraries/Arduino_LSM9DS1/blob/master/src/LSM9DS1.cpp)
