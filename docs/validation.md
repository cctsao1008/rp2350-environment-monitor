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

## P1 all-sensor firmware (initial hardware function PASS, 2026-10-28)

- [x] TSL2591 raw counts and estimated Lux reporting on hardware (approx 226 Lux); saturation and calibration unverified.
- [x] LTR390 ALS (3047-3049 counts), estimated Lux (~609), UV=0 visible on hardware; UV stimulus response and accuracy unverified.
- [x] SGP40 SRAW data present, 65535 first sample and 24663 by ninth, algorithm warming indication.
- [ ] VOC Index after 45-second startup and response under stimulus; first SRAW at 65535 needs steady-state confirmation.
- [x] ICM20948 accel/gyro and AK09916 magnetic XYZ data present (static gravity near 1g); motion/bias/calibration test pending.
- [x] Nine 1 Hz integrated USB CDC records with 1000 ms uptime increments, no errors observed in captured excerpt.
- [ ] Error handling/failure injection and extended stability.

Details: [P1 all-sensor validation plan](p1-all-sensors.md).

## Future P1 / P2

- [x] BME280 compensated-data acquisition: initial on-target functional test PASS (33 successive readings). See [P1 BME280 results](p1-bme280.md).
- [x] Remaining sensor drivers supply initial real data on the integrated board (9 records); calibration and stimulus-response not yet qualified.
- [ ] VOC Index with temperature/humidity compensation.
- [x] USB CDC sample counter and millisecond uptime provided (not wall-clock time).
- [ ] microSD data logging.

Do not report PASS without corresponding captured hardware evidence.

## Raspberry Pi Sense HAT V1 — LED blanking (hardware PASS 2026-10-28)

- [x] Sense HAT V1 controller at `0x46` confirmed indirectly by successful LED clearing (the firmware checks ID `0xF0` = `'s'` before writing).
- [x] User reports RGB matrix LEDs turn off after flashing the LED-clearing firmware (2026-10-28); continuous-off duration not measured.
- [x] Existing Environment HAT still reports 6/6 initialized and eleven consecutive 1 Hz records with no errors in the captured excerpt.
- [ ] Confirm Sense HAT controller / EEPROM has not been modified (only volatile framebuffer writes 0..191).

The default rainbow comes from the ATtiny88 built-in pixel array; Linux normally clears it during framebuffer setup. The RP2350 firmware now explicitly clears the same volatile display data without Linux.


## Raspberry Pi Sense HAT V1 — additional sensor identities

Status: **DEVICE ID HARDWARE PASS (4/4), 2026-10-28**.

- [x] LSM9DS1 accel/gyro: I2C `0x6A`, `WHO_AM_I(0x0F)=0x68`.
- [x] LSM9DS1 magnetometer: I2C `0x1C`, `WHO_AM_I(0x0F)=0x3D`.
- [x] LPS25H pressure: I2C `0x5C`, `WHO_AM_I(0x0F)=0xBD`.
- [x] HTS221 temperature/humidity: I2C `0x5F`, `WHO_AM_I(0x0F)=0xBC`.
- [x] Existing Environment HAT reported BME280, TSL2591, LTR390, SGP40, ICM20948 and AK09916 initialized and sampled.

The four checks run **once at boot**, after the existing one-time I2C probe,
without changing configuration or calibration registers. A missing or
unexpected ID must remain a FAIL/PENDING, not an inferred device identity.


### Captured initial Sense HAT V1 hardware evidence

```text
Sense HAT V1 LED matrix: OFF (framebuffer cleared)
I2C initial read-probe (diagnostic only): 0C 1C 28 29 46 53 5C 5F 68 6A 76
Sense HAT V1 sensor identities (WHO_AM_I 0x0F):
  LSM9DS1_AG   at 0x6A: ID 0x68 (PASS; expected 0x68)
  LSM9DS1_MAG  at 0x1C: ID 0x3D (PASS; expected 0x3D)
  LPS25H       at 0x5C: ID 0xBD (PASS; expected 0xBD)
  HTS221       at 0x5F: ID 0xBC (PASS; expected 0xBC)
Sense HAT V1 sensor ID summary: 4/4 PASS
INIT BME280=OK TSL2591=OK LTR390=OK SGP40=OK ICM20948=OK AK09916(mag)=OK
[sample=1 uptime=3502ms]
BME280  T=25.16C RH=61.36% P=1008.87hPa
TSL2591 CH0=62 CH1_IR=19 visible=43 lux=4.87 (estimate)
LTR390  UV=0 UVI=0.00 ALS=56 lux=11.20 (estimates)
SGP40   SRAW_VOC=29654 VOC_Index=WARMING_UP (about 45s; not ppm)
ICM20948 ACCEL[g] x=0.042 -0.025 1.011 GYRO[dps] x=-1.954 0.947 1.328
AK09916  MAG[uT] -4.95 17.10 36.00 (uncalibrated)
[sample=11 uptime=13502ms]
BME280  T=25.19C RH=61.14% P=1008.89hPa
SGP40   SRAW_VOC=29640 VOC_Index=WARMING_UP (about 45s; not ppm)
```

The sample timestamps cover eleven consecutive samples in 10 seconds,
at a 1000 ms nominal cadence. Device identity PASS does **not** imply
measurement/calibration PASS.

### New Sense HAT V1 data acquisition implementation

**Status: SOURCE IMPLEMENTED; new measurement drivers and configuration
not yet compiled or validated on the target.**

- [ ] HTS221: read factory calibration, measure T and RH, compare with BME280.
- [ ] LPS25H: measure pressure / internal temperature, compare pressure with BME280.
- [ ] LSM9DS1: accel / gyro / mag values are physically plausible; check orientation.
- [ ] Cross-HAT bias, self-heating and axis alignment analyzed; do not average readings yet.
- [ ] Confirm all previous Environment HAT sensors remain functional after integration.
- [ ] Minimum 60s run including post-blackout SGP40 VOC Index.


## Sense HAT V1 scrolling LED marquee and HTS221 diagnostic (new FW)

**New source only; built and on-device function not yet verified.**

- [ ] Startup remains LED OFF (the previously verified LED clear remains).
- [x] User confirmed Sense HAT V1 LED marquee works on physical hardware (`LED OK`, 2026-10-28); exact speed/brightness and automatic end-of-message blanking not separately quantified.
- [ ] USB CDC `led b 1` reduces brightness; `led off` immediately blanks LEDs.
- [ ] USB CDC `led TEXT` scrolls up to 48 ASCII characters; unknown glyphs show `?`.
- [ ] Concurrent 1 Hz sensor samples remain stable; no I2C/NACK regressions.
- [ ] HTS221 factory coefficients printed once; on out-of-range humidity, show unclamped RH and raw ADC instead of treating clipped 100% as valid.
- [ ] Compare BME280 and HTS221 RH; actual fault origin not yet identified.

The newly captured 48 samples showed HTS221 RH stuck at 100.00 %RH while
BME280 ranged around 62 %RH. LPS25H pressure was about 2.5 hPa above
BME280; this needs reference verification, not arbitrary offset removal.
Both IMUs were responsive to manual rotation. SGP40 VOC Index exited initial
blackout at sample 47 but index value 1 is not enough to validate air quality.


## Sense HAT LED marquee PASS / HTS221 sensor diagnostic (2026-10-28)

The user reports **LED OK** with firmware `f29842a`.
The accompanying excerpt has 16 records (samples #3..#18), 16 scheduler
warnings, and 15 inter-sample intervals of 1002..1003 ms (mean 1002.6 ms).
Original sleep loop uses `sleep_ms(5)` and reports any past-deadline arrival;
this is consistent with software-induced 2..3 ms lateness, rather than
proving that sensor acquisition takes longer than 1000 ms.

HTS221 RH is **INVALID for all 16 records**, with exactly
`humidity_raw=-32768 (0x8000)` and `interp=136.94 %RH`, while BME280
reports about 58.7 %RH. Both chips' temperature readings remain responsive.
This is not a safe indication of 100% RH; **never feed HTS221 RH into
fusion or use it for SGP40 compensation**. Keep BME280 as the current
valid source. The humidity sensor IC / calibration may be faulty, but the
cause is still unproven.

ST's official forum includes a historic case of **HTS221 RH ADC=-32768
with sensible temperature**. ST advises checking two-point interpolation
and considering device calibration if the calculation is sound:
https://community.st.com/mems-sensors-48/in-hts221-sensor-not-getting-correct-humidity-data-95288

### Follow-up source modification (verification pending)

- [ ] Run firmware with revised deadline scheduler (1ms coarse sleep and
      precise `sleep_until` near the 1Hz boundary); confirm no repeated
      false `WARN` lines and stable 1000ms timestamps.
- [ ] Verify new read-only HTS221 register diagnostic output:
      `burst_raw`, individual `H_OUT_L/H_OUT_H`, data-ready
      `STATUS_REG`, `CTRL_REG1`, `CTRL_REG2`, `AV_CONF`.
- [ ] Capture `HTS221 CAL:` from startup.
- [ ] If both burst and independent byte reads repeat `0x8000`, keep
      humidity classified as invalid and prioritize hardware / calibration
      investigation over changing the interpolation formula.

The LED marquee code itself is unchanged in this follow-up.

## HTS221 read-only snapshot + one-shot (implementation only)

- [ ] Verify user-invoked `hts diag` read-only snapshot on target.
- [ ] Verify user-invoked `hts oneshot` one-shot conversion with
      explicit guard and restoration of 7 Hz continuous operation.
- [ ] Confirm HTS221 RH diagnosis without changing other sensor values.
- [ ] Do not infer chip damage from the undocumented 0x8000 raw code.


## 2026-10-28 HTS221 initial fault isolation on hardware

- [x] WHO_AM_I 0xBC; CTRL1 0x86, CTRL2 0x00, heater off.
- [x] AV_CONF 0x3F observed (max averaged; not factory reset 0x1B).
- [x] Burst and single byte reads agree on persistent 0x8000 in most cases.
- [x] One continuous read showed 0x4065 (16485), also invalid after calibration.
- [x] One of several manual one-shot trials timed out with H_DA=1, T_DA=0;
      others returned 0x8000; none validates humidity.
- [x] Normal 1Hz cadence: 50 of 51 observed intervals = 1000ms;
      one 1035ms interval followed a manual diagnostic and a 34.2ms lateness warning.
- [ ] Cold power-cycle, then `hts diag`, inspect AV_CONF default.
- [x] `hts avgtest` controlled 0x1B comparison and rollback hardware PASS (three runs, 22 Hraw samples).

## 2026-10-28 HTS221 averaging experiment closure

- [x] Three explicit `hts avgtest` runs: temporary `AV_CONF=0x1B` (factory
      default), confirmed return to original `0x3F` each time.
- [x] 22 valid raw humidity register readings under 0x1B:
      min -32754, max -31654; **all invalid against factory calibration**.
- [x] Two NOT_READY sample slots during the manual tests, without lasting
      impact on subsequent environmental-sensor acquisition.
- [x] HTS221 temperature responsive, but humidity trends into the signed
      16-bit lower limit (-32768); BME280 indicates ~52..54% RH.
- [x] Exactly three large scheduler lateness warnings (>0.9s) attributable
      to 1.6s manual tests; normal nominal 1Hz samples unaffected.
- [ ] Independent humidity reference / controlled dry-air test (optional).
- [ ] Verify true power removal before treating post-reset AV_CONF=0x3F
      as evidence of an abnormal power-on default.

**Verdict: averaging hypothesis ruled out as sufficient cause.**
HTS221 humidity remains INVALID; do not use RH in fusion, logging as
valid data, or SGP40 compensation. Retain BME280 RH. Preserve source
and diagnostic functionality; no additional FW tuning scheduled.


## HTS221 complete register dump (manual USB CDC)

- [ ] `hts dump` command runs on RP2350 and prints documented register map,
      16 calibration bytes with addresses, raw H/T and decoded values.
- [ ] Read-only controls and calibration; `STATUS.H_DA/T_DA` read-to-clear
      behavior via high output bytes is documented and observed.
- [ ] Cross-check raw and calibration with prior `hts diag` captures.
