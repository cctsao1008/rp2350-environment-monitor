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

- [x] `hts dump` command runs on RP2350 and prints documented register map,
      16 calibration bytes with addresses, raw H/T and decoded values (3/3).
- [x] Read-only controls and calibration; `STATUS.H_DA/T_DA` read-to-clear
      behavior via high output bytes observed (STATUS 0x03 -> 0x00).
- [x] Cross-check raw and calibration with prior `hts diag` captures: RH raw 0x8000,
      Traw 343/346, factory bytes and compensation consistent.

## HTS221 diagnostic dump results — 2026-10-28

- [x] Three `hts dump` runs end with `status=OK`.
- [x] `WHO_AM_I=BC`, `AV_CONF=3F`, `CTRL1=86`, `CTRL2=00`,
      `CTRL3=00`.
- [x] `STATUS 0x03` before read and `0x00` after read.
- [x] `0x28=00`, `0x29=80`: humidity output `0x8000 (-32768)`
      in all three dumps.
- [x] Traw `343,343,346` and temperature `25.49,25.49,25.55°C`
      continue updating.
- [x] Calibration `3B 89 99 0D 00 C4 FB FF 65 03 86 D1 00 00 0E 03`
      matches earlier snapshots; RH interpolates to 136.94%, INVALID.
- [x] BME280 reports ~62.4..62.9%RH during same interval.
- [x] Sample timing remains nominal 1Hz in the captured interval.
- [ ] Verify isolated hardware/known humidity reference if required.

**Disposition:** HTS221 RH sensor channel untrustworthy; preserved invalid
status and BME280 compensation path; no firmware changes to calibration
or measurement algorithms.


## HTS221 automatic bounded recovery experiment (source-only)

- [x] On-device boot auto-start with no CLI command (experimental build).
- [x] Three 10s heater ON intervals, 30s cooldown, 5s logs.
- [x] No HTS221 data reported as valid during heat or cooldown.
- [x] Other sensor acquisition continued at 1Hz; heater stopped after three cycles (LPS25H internal temperature did rise locally).
- [x] Heater OFF readback verified 3/3; OFF failure/retry path not fault-injected, remains source-only.
- [x] HTS221 post-heat Hraw stays -32768 / 136.94% INVALID; BME280 ~58.5–58.8% RH.

### Heater experiment closure — user device, 2026-10-28

- [x] 157 consecutive 1Hz records #5–#161; 156/156 intervals 1000ms.
- [x] 3 heater ON verified and 3 OFF verified, cooldown complete, DONE.
- [x] 37 validly read HTS221 raw humidity records, all -32768;
      25 post-third-cycle records unchanged.
- [x] HTS221 RH is still INVALID; do not attempt numerical correction.
- [x] LPS25H temperature transient ~+0.9°C per heater pulse
      (local thermally coupled stack), BME280 substantially stable.
- [ ] Serial terminal ended with an unclassified device read error;
      no evidence to distinguish cable, host software, USB CDC or reset.
- [ ] New default observation-only firmware hardware check after flashing:
      boot says OBSERVE_ONLY, 5s status lines, no heater activation.

**Default FW disposition:** disable repetitive 3-cycle heater test
on future boots; keep 5s autonomous logs, HTS221 invalid masking, and
all other sensor drivers.


## Sense HAT V1 LED OFF at RP2350 reset / cold power-on (new boot code)

**Source implemented; both reboot modes pending hardware verification.**
The previously working single startup LED clear was expanded to seven
I2C0x46 identity-checked volatile framebuffer clears (t=0, 50, 150, 350,
750, 1500, 3000ms relative to early boot). This replaces, rather than
adds to, the original three-second USB CDC connect wait. The final clear
result is used to enable the optional `led demo` console controls.

- [ ] RP2350-only software reset: matrix returns to OFF even after a
      manual `led demo` was running.
- [ ] Full USB disconnect / power-on: matrix ends OFF without a command.
- [ ] No LED pattern is reintroduced after ATtiny88 startup.
- [ ] `led demo` continues to work; sensor cadence remains 1 Hz.

**Hardware constraint:** Firmware cannot ensure zero light at the exact
instant of power application; ATtiny88 may display its built-in default
pattern before RP2350 can talk to it. For strict instantaneous darkness,
ATtiny88 firmware or LED supply/enable hardware must be changed.


## 2026-10-28 HTS221-only console output filter

- [x] Source contains `HTS221_ONLY_OUTPUT=1` and a shared HTS221
      output helper to prevent full-mode vs narrow-mode drift.
- [x] Console 1Hz output includes only HTS221 and sample timestamp;
      startup suppresses other sensor boot scan, IDs and INIT lines.
- [x] Existing sensor_manager initialization, sampling, BME280-fed SGP40,
      Sense HAT LED clearing and HTS221 automatic 5s observation unchanged.
- [ ] Build via `scripts/build.ps1`, flash, verify HTS221-only output on
      RP2350 and nominal 1Hz interval. (Note: intended device is HTS221.)
- [ ] Restore complete display by setting `HTS221_ONLY_OUTPUT=0` after
      the investigation.


## HTS221 verification-first gate — pending real board evidence

- [x] Existing real target logs: I2C identity, factory calibration,
      byte/burst matching, 7Hz data, on-demand averaging and heater test.
- [x] Source: independent ONESHOT H_DA/T_DA readiness and restore.
- [x] Source: CTRL2.BOOT self-clearing bit poll and configuration restore.
- [ ] **Build** latest source with Pico SDK 2.3.1.
- [ ] **Flash** latest image via picotool.
- [ ] Verify fresh `HTS221 VERIFY BEGIN` register values.
- [ ] Verify `BOOT: bit_self_cleared=YES`, calibrated Hraw pre/post,
      factory calibration-byte consistency and configuration restore.
- [ ] Verify ONESHOT H_DA and T_DA independently, capture each latency
      and raw, confirm restore PASS.
- [ ] Verify POST_ONESHOT continuous Hraw without altering RH validity.
- [ ] Confirm normal 1Hz HTS221-only output after the self-test and
      5s OBSERVE_ONLY without heater activation.
- [ ] Confirm complete power removal versus RP2350 reset separately;
      collect AV_CONF values for both conditions.
- [ ] Diagnose any self-test FAIL/RESTORE_FAILED by its exact logs,
      not by extrapolating from raw=0x8000.


## 2026-10-28 HTS221 BOOT and independent ONE_SHOT real-device result

- [x] `BOOT` flag clears automatically; CTRL_REG2=0x00.
- [x] Public calibration bytes identical across BOOT operation.
- [x] PRE_BOOT, POST_BOOT, POST_ONESHOT Hraw all -32768 (0x8000).
- [x] Independent H_DA and T_DA both assert at 20ms; Hraw=-32768,
      Traw=379, no temperature-ready dependency timeout.
- [x] ONE_SHOT bit clears and CTRL1=0x86 CTRL2=0x00 AV_CONF=0x3F
      restored with readback.
- [x] 19 consecutive 1Hz HTS221-only frames / 18 intervals all 1000ms.
- [ ] Internal BOOT trim contents not externally inspectable.
- [ ] New source-only preinit snapshot of AV_CONF/CTRL1/2/3 hardware test.
- [ ] Explicit full power-removal vs RP2350-only reset comparison.

**Important:** HTS221 Rev5 Table 15 default AV_CONF=0x1B; the user
observed AV_CONF=0x3F after init. This must be isolated BEFORE assuming
the source of the difference. The new preinit snapshot is read-only.

## 2026-10-28 NEW EVIDENCE — HTS221 humidity raw RECOVERED after external warm air

**Supersedes the earlier "RH stuck at -32768" *current-state* finding,
but does not invalidate the earlier tests.** The user applied a hair
dryer (external warm air) and immediately shared a live USB CDC capture.
The firmware, original calibration, and BOOT/ONE_SHOT test code were
unchanged.

| Evidence | Before warm air | After warm air |
| --- | --- | --- |
| Humidity raw | -32768 / 0x8000 (repeated) | -7616 @ ~8.5s; -7683 @ ~13.5s |
| Humidity interpolation | 136.94%RH / INVALID | ~54.46..54.81%RH / VALID |
| HTS221 temperature | ~26.4°C | 57.33°C declining to 54.10°C |
| Internal heater | Prior 3x 10-second tests, no recovery | OFF, verified; no automatic heating |
| Factory calibration | H0=29.5, H1=68.5, R0=-5, R1=-11898 | Unchanged |

The raw humidity output **is not permanently pinned** to 0x8000,
and using the existing factory calibration produces bounded values
after external heating. The earlier proposal to alter factory calibration
was not required to change Hraw, and changing these coefficients would
never directly change the raw ADC output.

**Critical limitation:** RH_VALID=YES here means only a *plausible
numeric interpolation*, not proven measurement accuracy. The device
was 54..57°C, far above local baseline ~26°C and beyond the 15..40°C
range of the datasheet's tight temperature accuracy specification.
Humidity readings around 54.6%RH during strong heating cannot by
themselves validate ambient RH: the local air and sensed surface can
have different temperature, water content and thermal time constants.

ST sources:
- HTS221 Rev5 Sec. 7.4: onboard heater for recovery from condensation,
  measure *after* heater is off
  https://www.st.com/resource/en/datasheet/hts221.pdf
- ST MEMS engineer, 2020 heater explanation:
  https://community.st.com/mems-sensors-48/is-there-an-app-note-or-other-documentation-related-to-how-when-to-appropriately-use-the-heater-bit-on-the-hts221-regards-regards-43760
- Rev5 Sec 4.1: re-hydration after soldering at 25°C/55%RH for 3 days
  or 70%RH for 12 hours; such numbers apply to soldering recovery,
  not a prescribed requirement for this warm-air experiment.

**Revised diagnosis:** response to external warmth is strong evidence
of a *thermally reversible* condition; retained moisture/condensation
or contamination is plausible but unproven. This is **not** proof of
permanent semiconductor damage, and we still cannot distinguish
sensor-film behavior from PCB leakage or localized thermal effects.

- [x] Confirm Hraw escapes the extreme lower endpoint with no
      calibration rewrite; derived RH changes accordingly.
- [x] Confirm heater remains OFF in warm-air capture.
- [ ] Let the board return to ambient temperature without additional
      directed heating; capture Hraw, RH, T for at least 15-30 minutes.
- [ ] Compare settled values to the nearby BME280 measured at the
      same location/temperature (reference is comparative, not a
      laboratory accuracy standard).
- [ ] Repeat at ambient after a real full power disconnect and
      reapply; inspect HTS221 PREINIT 0x10, 0x20, 0x21 and 0x22.
- [ ] Apply a **non-condensing** humidity change and check monotonic
      direction and repeatability of Hraw across multiple points.
- [ ] If RH relapses at ambient, preserve logs and distinguish thermal
      dependence versus humidity-dependent behavior before diagnosis.
- [ ] Avoid more hair-dryer exposure pending cool-down observation.

No firmware change is needed for this observation. HTS221-only output
and the validity flag are preserved; the observation is evidence
that future software-calibration proposals must first confirm actual
dynamic RH response.


## 2026-10-28 follow-up: warm-air recovery did NOT persist during cooldown

Source: user-supplied USB CDC capture, sample #2–#231, uptime
4907–233907ms (229s). The sample portion has **230 complete
records**, all 229 consecutive sample intervals exactly 1000ms.
The 5-second `HTS221 AUTO` lines continue to show
`phase=OBSERVE_ONLY`, `HEATER_AUTO=DISABLED`.

| Measurement point | Sensor T | Interpolated RH | Hraw | Interpretation |
| --- | ---: | ---: | ---: | --- |
| #2 (4.907s) | 57.33°C | 54.65% | not printed | within 0–100 display gate |
| ~#5 (8.491s AUTO) | ~56°C | ~54.5% | -7616 | dynamic raw, VALID |
| #55 (57.907s) | 44.70°C | 63.03% | not printed | VALID |
| #100 (102.907s) | 38.10°C | 73.16% | not printed | VALID |
| #160 (162.907s) | 33.87°C | 87.29% | not printed | VALID |
| #220 (222.907s) | 31.52°C | 99.18% | not printed | near upper gate |
| **#221 (223.907s)** | **31.37°C** | **100.14%** | **-21546 (0xABD6)** | **first INVALID** |
| #231 (233.907s) | 31.07°C | 101.96% | -22102 | INVALID, raw still dynamic |

Note: the independent-byte verification at #221 reports
`single_L=0xD6 single_H=0xAB`, equal to burst
`-21546`. This is not the earlier constant `0x8000` endpoint.
Last 11 frames (#221–#231) are INVALID; prior 219 frames are within
the simple 0–100% output-range gate.

Across the measured cooling window, sensor-reported temperature
declined 26.26°C and computed RH increased 47.31 percentage points,
with Pearson correlation ~**-0.935**. This association does not prove
a specific physical mechanism: a directed hot-air exposure introduces
non-equilibrium vapor, sensor-film and PCB temperatures, and possible
moisture absorption/desorption. Neither an ambient reference nor a
controlled dew-point/humidity source is present in this capture.

**Correction to earlier disposition:** external heating temporarily
made Hraw variable and the computed RH numerically bounded, but did
**not** demonstrate durable or accurate recovery. By 31.37°C it was
already outside the humidity operating range, long before returning to
the original ~26°C ambient baseline. A `RH_VALID=YES` result in this
firmware is a numeric-range check, not an accuracy guarantee.

ST HTS221 Datasheet Rev5 Table3:
https://www.st.com/resource/en/datasheet/hts221.pdf
lists 0–100% operating RH, non-condensing accuracy conditions,
and humidity response t@63% of ~10 seconds measured at 25°C.
Relevant comparison case (heater temporarily lowers RH then it rises):
https://community.st.com/mems-sensors-48/why-are-my-hts221s-getting-bad-temperature-values-25361

Next tests, still pending:
- [ ] Let the sensor cool naturally to stable ambient, avoid more hot-air
      heating, and continue capturing both raw and unclamped RH values.
- [ ] Compare at the *same settled temperature* against a colocated
      reference (e.g. BME280), accounting for spatial temperature
      gradients and prior heating.
- [ ] Observe whether Hraw goes back to -32768, or remains dynamic
      while RH is >100%, and record the associated temperature/time.
- [ ] After cold stabilization, truly remove all power, reboot, and
      compare PREINIT AV_CONF/CTRL1 against prior MCU-only reset logs.
- [ ] Do not edit factory calibration or mask invalid RH to force a PASS.
- [ ] Mechanism/root-cause declaration remains PENDING.


## 2026-10-28 extended cooldown log: 500 records through sample #501

Second uploaded log extends the same post-hair-dryer cooling observation
from `sample=2` through `sample=501`. There are **500 validly
parsed output frames**, **499/499 gaps = 1000ms** (uptime 4907–
503907ms; elapsed 499 seconds). `OBSERVE_ONLY` remains in effect,
with `HEATER_AUTO=DISABLED`; no changes to the firmware or factory
calibration occurred in the observed data.

| Milestone | Sample | T | Computed RH | Hraw |
| --- | --- | --- | --- | --- |
| Start | 2 | 57.33°C | 54.65% | not printed |
| Numeric range boundary | 220 | 31.52°C | 99.18% | not printed |
| First INVALID | **221** | **31.37°C** | **100.14%** | **-21546** |
| Midpoint | 301 | 29.72°C | 115.25% | -26154 |
| Late | 401 | 28.61°C | 125.01% | -29132 |
| End | **501** | **28.05°C** | **128.24%** | **-30117** |

- Frames #2..220: **219** within the firmware numeric validity
  gate (0–100%RH); this does **not** validate accuracy.
- Frames #221..501: **281 consecutive INVALID**, all due to
  computed RH exceeding 100% (no raw=0x8000 was reported in
  these 281 records).
- Last **60** samples: T mean **28.162°C** (min 28.05,
  max 28.27°C), interpolated RH mean **128.127%**
  (min ~126.74, max 129.43%). Temperature nearly stable but
  numeric RH invalid throughout.
- Entire 500-point temperature/RH correlation approximately
  **-0.916**. This describes an uncontrolled cool-down and
  is **not** a temperature correction or proof of causality.
- Latest Hraw `-30117` is ~92% of the way from zero to the
  signed-16 negative endpoint `-32768`; it **has not**
  reached that endpoint again in this log.
- Confirmed independent byte vs burst reads for invalid sample
  #221: `0xAB 0xD6` reconstructed as `-21546`,
  supporting correct sign/byte-order handling.

**Interpretation:** Warm air produced a temporary numerical-range
recovery and a dynamic humidity raw output, followed by a continuous
re-entry into invalid relative humidity during cooling. The near-
ambient tail still has a large positive RH error (assuming normal
ambient conditions). There is no demonstration that temperature
alone causes this; condensation, surface-film response, localized
thermal gradients or leakage remain unseparated hypotheses.

**No calibration write or software-recalibration is justified by
this trace.** Changing mapping coefficients can rescale reported RH
but cannot repair raw data or establish accuracy; a minimum of two
controlled humidity reference states at a *stable common temperature*
is needed to fit and verify a new software mapping.

Pending:
- [ ] Allow a longer steady ambient period without heating; retain
      Hraw and unclamped RH and watch for renewed `-32768`.
- [ ] Concurrent reference BME280 T/RH at a shared location, preferably
      without adding I2C/sampling noise (the current HTS221-only mode
      continues BME280 acquisition but hides its console values).
- [ ] Controlled non-condensing humidity perturbation at constant T;
      assess raw repeatability, slope and recovery after humidity reversal.
- [ ] True all-supplies-off startup vs RP2350 reset, record PREINIT
      AV_CONF and CTRL1/2/3.
- [ ] Hardware root cause remains undetermined.
