# Validation record

Status: **PARTIAL HARDWARE VALIDATION — P0 ongoing**.

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
- [ ] TSL2591, LTR390, ICM20948 identity registers (new diagnostics; pending on-board run).
- [ ] Sensor measurement data and calibration validated.

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

### Next targeted test

**Observed on hardware:** SGP40 Get Serial Number `0x3682`, address `0x59`, 9-byte response, all 3 CRC-8 words passed; serial `000007C138AD`. The previous apparent address mismatch is **fully explained** by SGP40 command semantics and TSL2591's secondary read-only address.

**Next:** Validate three remaining sensor IDs using device-specific reads:
- TSL2591 (0x29): ID register 0x12 through command byte 0xB2, expected `0x50`.
- LTR390 (0x53): PART_ID register 0x06; high nibble `0xB` (low nibble is revision).
- ICM20948 (0x68): select bank 0, WHO_AM_I register 0x00, expected `0xEA`.

Each check is implemented in `src/main.c`, not yet built or run on the physical board.

## Future P1 / P2

- [ ] Sensor drivers, calibration and data validity.
- [ ] VOC Index with temperature/humidity compensation.
- [ ] Timestamped USB CDC telemetry.
- [ ] microSD data logging.

Do not report PASS without corresponding captured hardware evidence.
