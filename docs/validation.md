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
- [ ] Other sensor identities and actual data validated.

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

**Unresolved:** 0x28 responded unexpectedly and 0x59 did not respond to an *uncommanded one-byte read*. SGP40's read header may be NACKed until a command is issued. A generic read-probe count must not be interpreted as the number of confirmed sensors.

### Next targeted test

Firmware now issues **SGP40 Get Serial Number** command `0x3682` to address `0x59`, waits 2 ms, reads 9 bytes, and verifies 3 CRC-8 bytes. Use this to determine whether the SGP40 is functional. An ACK at 0x28 remains unexplained until follow-up testing; do not label it as a different sensor without an identity check.

## Future P1 / P2

- [ ] Sensor drivers, calibration and data validity.
- [ ] VOC Index with temperature/humidity compensation.
- [ ] Timestamped USB CDC telemetry.
- [ ] microSD data logging.

Do not report PASS without corresponding captured hardware evidence.
