# Validation record

Status: **NOT YET VALIDATED ON PHYSICAL HARDWARE**.

## P0 bring-up acceptance

- [ ] Windows host prerequisites installed and recognized by scripts/build.ps1 -Mode check.
- [ ] Pico SDK 2.3.1 and nested submodules available locally.
- [ ] Offline CMake configure completes without downloading sources or host tools.
- [ ] Firmware builds and links (ELF generated).
- [ ] Second build with no changes is incremental and network-free.
- [ ] Flash over PICOBOOT using picotool, then verify and reboot (no MSC file copy).
- [ ] USB CDC console prints startup banner.
- [ ] I2C probe reports expected responders (may require device-specific probing).
- [ ] BME280 register 0xD0 returns chip ID 0x60.
- [ ] Application-triggered BOOTSEL reset disables MSC while retaining PICOBOOT.
- [ ] Recovery procedure documented for failed applications / hardware BOOTSEL.

No PASS result should be reported without measured evidence.

## Future P1 / P2

- [ ] Sensor drivers and calibration.
- [ ] VOC Index with temperature/humidity compensation.
- [ ] Timestamped USB CDC telemetry.
- [ ] microSD data logging.
