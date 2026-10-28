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


## Measurement quality update — HTS221 RH (2026-10-28)

The first extended on-device record shows Sense HAT V1 HTS221 temperature
around 26.5 °C, but **humidity_raw=-32768 constantly**, with unclamped
factory interpolation 136.94 %RH versus BME280 about 58.7 %RH. Status:
**HTS221 RH INVALID**. Do not average with BME280. The underlying cause
has not yet been isolated. A subsequent firmware change adds independent
single-register reads and I2C configuration/status diagnostics.

The LED text marquee is **hardware functional PASS** (user: `LED OK`).
An unrelated 5ms sleep granularity issue in the main scheduler produced
warnings at each sample despite only ~2.6ms average period excess;
follow-up firmware improves its deadline handling, still pending on-target
verification.


## HTS221 controlled fault-isolation (2026-10-28, source pending target test)

Implemented USB CDC diagnostic commands; no extra software or HAT required.

- `hts diag`: **read-only**, print WHO_AM_I, CTRL_REG1/2, HEATER bit,
  AV_CONF, calibration bytes `0x30..0x3F`, STATUS before output read,
  humidity+temperature burst output `0xA8..0xAB` and separate
  `0x28` / `0x29` readings. Since the source runs at 7 Hz, the
  burst and separate reads can theoretically span a conversion boundary:
  interpret slight differences with caution.
- `hts oneshot`: **explicit opt-in**. Only proceeds if
  `WHO_AM_I=BC`, `CTRL_REG1=0x86` and `CTRL_REG2=0` (heater OFF).
  Switches to one-shot `CTRL_REG1=0x84`, drains older output,
  triggers `CTRL_REG2.ONE_SHOT=1`, polls `STATUS[1:0]=3`
  for up to 600 ms, captures one-shot humidity and temperature, then
  restores the previous continuous 7 Hz configuration. Does **not**
  use BOOT, enable the heater, or write calibration/EEPROM registers.
  A manual one-shot may perturb the current one-second schedule;
  steady-state mode remains unchanged.
- If restore fails, HTS221 collection is disabled until power-cycle to
  avoid silently continuing with a changed measurement configuration.
- `0x8000` is a signed 16-bit minimum value, **not an officially
  documented HTS221 error code**. It remains invalid for the observed
  physical comparison and repeated saturation. Diagnose independently
  of the calibration interpolation.

### Acceptance

- [ ] `hts diag` confirms ID=0xBC, CTRL1=0x86, heater=0.
- [ ] Single-register humidity agrees with burst humidity across stable
      measurements or identifies a burst/read discrepancy.
- [ ] `hts oneshot` captures fresh data with H_DA/T_DA high, and logs
      restoration to continuous mode.
- [ ] Sense HAT V1 LSM9DS1, LPS25H and Environment HAT continue
      to acquire after the manual one-shot test.
- [ ] If one-shot and continuous both report 0x8000, classify HTS221 RH
      as unresolved hardware/measurement-chain fault; do not force
      software correction or report 100%RH as valid.

Authority: ST HTS221 Datasheet DocID026333 Rev 5 (2023), sections 5.1.1,
7.3, 7.4, 7.6–7.8 and 8.


### Follow-up averaging comparison — new experiment, 2026-10-28

The user ran the first diagnostic firmware. ID=0xBC, CTRL1=0x86,
CTRL2=0x00/heater OFF, but AV_CONF=0x3F, which is the maximum-averaging
setting (temperature 256 samples and humidity 512). ST Rev 5 Table 16
specifies **AV_CONF default 0x1B** (temperature 16 and humidity 32).
The firmware does not set AV_CONF during ordinary initialization. This
could represent a state retained when only the RP2350 resets and HAT
power remains applied; the cause of the setting is not established.

The repeated raw humidity was 0x8000 during both continuous and one-shot
measurements. One continuous diagnostic after a one-shot TIMEOUT instead
produced 0x4065 (16485), which evaluates to approximately -24.57%RH under
that unit's supplied factory calibration, also invalid. The one-shot
TIMEOUT was H_DA=1/T_DA=0; at maximum averaging this does **not** by
itself prove permanent sensor hardware failure. One manual diagnostic
induced 34,212us deadline lateness, while all other adjacent logged
samples maintained 1000ms cadence.

Added `hts avgtest`: manual, opt-in comparison in **7Hz continuous mode**.
It verifies ID, CTRL_REG1 and CTRL_REG2, writes **only** AV_CONF to
factory-default 0x1B, verifies readback, samples eight times 200ms apart
(with status checks and unmodified humidity/temperature raw registers),
then restores the exact previous AV_CONF and verifies restoration. It
will not write the heater, BOOT bit, calibration or reserved registers.
If restore verification fails, it prevents subsequent HTS221 reads until
reset. The deliberate ~1.6-second diagnostic may delay a 1Hz sample.

- [ ] Re-test following complete USB power disconnect / cold power-on;
      inspect AV_CONF before writing any configuration.
- [x] Run `hts avgtest`: three 8-slot trials, 22 raw measurements and two NOT_READY events. All measured humidity raw values remain invalid (-32754..-31654).
- [x] All three trials report `DONE; AV_CONF restored to 0x3F`; other sensors continue reporting.

### 2026-10-28 averaging comparison — ON-DEVICE COMPLETE

**HTS221 RH channel: INVALID; software averaging adjustment NOT A FIX.**

In the 2026-10-28 user-supplied log, three manual `hts avgtest`
executions switched `AV_CONF` from observed `0x3F` (T=256/RH=512
internal averages) to `0x1B` (T=16/RH=32), collected 22 raw RH
values (`-32754` to `-31654`, mean ~`-32237`) and restored
`0x3F` with readback each time. Two sample slots returned
`NOT_READY status=0x00`; these alone are not evidence of a dead device.

Regular HTS221 sampling ranged from raw -31279 to -32768 after the
new capture started, with interpreted RH ~132..137% (invalid), while
BME280 RH was ~51.8..54.3%. HTS221 temperature ~23.1..23.3°C
tracked BME280 ~23.1..23.2°C. The humidity raw signal can change,
but is far beyond this unit's factory calibration points:

- H0: 29.5%RH / raw -5
- H1: 68.5%RH / raw -11898
- Predicted raw at BME280 ~52..54%RH: ~-6866..-7476
  (comparison is indicative; BME280 is not a calibrated humidity standard).

Negative calibration raw values are allowed; no arbitrary change to
coefficients or offset should be applied. The root cause is unresolved,
but sensor humidity measurement chain / element is substantially more
suspect than I2C byte order, factory-calibration decoding or
AV_CONF averaging. Treat HTS221 RH as unusable, retain HTS221 T, and
keep BME280 RH as the only current humidity source for SGP40
compensation. No more firmware tuning is justified by this evidence.

The ~0.93, ~0.93 and ~1.02 second `WARN` events coincide with
the explicit >1.6 second manual diagnostics, and are not evidence
of failure of the normal 1Hz scheduler. One HTS221 NOT_READY
following a manual test recovered on the next record.

**Remaining optional hardware isolation**: verify an actual full power
disconnect before inferring anything from `AV_CONF=0x3F` on restart,
or compare the Sense HAT module under a known humidity stimulus
with an independent reference. Do not change firmware calibration.


## HTS221 complete read-only register dump

**Status: source implemented, not yet verified on RP2350.**

The USB CDC command `hts dump` captures all defined HTS221 registers
from ST DocID026333 Rev 5 Table 15 and the complete 16-byte calibration
block. The output is **not** a contiguous memory dump of undocumented
reserved locations. Those ranges are intentionally skipped.

- Device: I2C 7-bit address 0x5F.
- Control/configuration: 0x0F WHO_AM_I, 0x10 AV_CONF,
  0x20 CTRL_REG1, 0x21 CTRL_REG2, 0x22 CTRL_REG3.
- Data-ready and outputs: 0x27 STATUS before and after data;
  one 4-byte auto-increment burst 0x28..0x2B for H/T raw ADC.
- Factory coefficients: every byte 0x30..0x3F with address,
  including reserved slots inside the calibration array.
- Parsed T0/T1/H0/H1 calibration points, 16-bit two's-complement
  raw values, unbounded humidity interpolation, temperature and
  validity flags; decode of AV_CONF, ODR, BDU, power state, HEATER.
- I2C reads only. No writes to any control, reserved or calibration
  registers. Reading HUMIDITY_OUT_H or TEMP_OUT_H clears the respective
  STATUS data-ready flag **by hardware design**; this is called out
  explicitly in the output.
- If a read fails the command terminates with a PARTIAL message.
  This operation can take extra time on a 100kHz shared I2C bus, but
  does not change the persistent configuration.
- The source is not an atomic snapshot of all registers; calibration
  and control are captured before status and sample bytes. Do not
  assert sample simultaneity across different sensors.

### Acceptance

- [x] The on-target `hts dump` logs all 0x0F/0x10/0x20..0x22,
      0x27..0x2B and 0x30..0x3F addresses (three successful dumps).
- [x] Raw H/T, unbounded RH and factory coefficients agree with
      previous `hts diag` values within ordinary sampling changes.
- [x] No control or calibration register was written; normal 1Hz
      environmental sampling continues (timestamps unchanged across dumps).

## 2026-10-28 HTS221 on-target register dump — PASS for diagnostic, FAIL for RH

The user captured three complete `hts dump` outputs from firmware
`792859c9`, at uptime_ms **10079, 14026 and 17354**.
All returned `DUMP END status=OK`.

| Field | Observed across all three dumps |
| --- | --- |
| WHO_AM_I (0x0F) | 0xBC |
| AV_CONF (0x10) | 0x3F |
| CTRL_REG1/2/3 (0x20..22) | 0x86 / 0x00 / 0x00 |
| STATUS before / after outputs | 0x03 / 0x00 (normal read-to-clear) |
| RH output (0x28..29) | 0x00 0x80 = **0x8000 (-32768)** |
| Temperature output (0x2A..2B) | 0x0157, 0x0157, 0x015A = 343, 343, 346 |
| RH calibration 0x30..3F | `3B 89 99 0D 00 C4 FB FF 65 03 86 D1 00 00 0E 03` |
| H0/H1 factory RH points | 29.5 / 68.5 %RH |
| H0/H1 output raw | -5 / -11898 |
| T0/T1 raw | 0 / 782 |
| T0/T1 degrees | 19.125 / 33.625 °C |
| RH from factory interpolation | **136.94% (INVALID)** |
| T from factory interpolation | 25.49, 25.49, 25.55 °C |
| Nearby BME280 RH | ~62.4–62.9% |

The humidity channel is consistently pinned to the *signed 16-bit minimum*
in this capture. Previous tests showed it sometimes outputs other values
(e.g. `-31279`.. `-32754`) on restart, still physically invalid. Do not
claim that it can **never** change. Sensor identity, data-ready, calibration
readback, and temperature conversion all work; **they do not prove the
humidity sensing element is electrically healthy**.

ST HTS221 Rev 5 Table 19 confirms the exact calibration register mapping;
Section 6 Table 15 confirms that CALIB registers must not be modified.
The documented register dump was *read only*, except the normal high-byte
read-to-clear behavior of STATUS; no persistent sensor register writes.
Earlier controlled `AV_CONF=0x1B` testing also failed to recover humidity.
Current root cause most consistent with humidity sensing path/element or
environmental damage, but not proved without independent hardware stimulus.

**Decision:** Keep HTS221 RH INVALID; use BME280 for environment humidity
and SGP40 compensation. No filter/offset correction justified. Firmware
unchanged; diagnostic / acquisition evidence recorded.


## Automatic HTS221 heater experiment — source implemented, hardware pending

At boot, after the normal HTS221 sensor ID/init checks, a finite,
non-blocking state machine automatically runs the following:

- **5 s baseline**, with existing 1Hz sampling intact.
- **10 s heater ON**, `CTRL_REG2 (0x21) bit1 = 1`; confirm readback,
  suppress HTS221 temperature/humidity acquisition entirely.
- **30 s cooldown** with heater turned OFF and verified. HTS221 T/RH
  remain withheld during the cooldown so heater-contaminated values
  are not represented as ambient.
- Repeat the bounded cycle at most **three times per boot**, stopping
  early if RH returns to the 0..100% valid range after cooldown. Then
  keep the heater OFF until firmware restarts.
- Every **5 seconds** a separate `HTS221 AUTO` line reports stage,
  last captured Hraw/Traw if available, and progress. After the
  third cycle, 5s reports continue without further heating.
- All other sensors, including BME280 for SGP40 compensation,
  continue the normal **1Hz** acquisition path.

Start-up safety gate requires `WHO_AM_I=0xBC`, `CTRL_REG1=0x86`,
`CTRL_REG2=0x00`. If an unexpected reboot has left the heater bit
ON, the firmware first attempts OFF and verifies it. Heater OFF
is checked after every pulse. Inability to verify OFF triggers an
automatic OFF retry every 250ms, keeps HTS221 data blocked and
prints a warning: **physically disconnect power if OFF cannot be
confirmed**. Hardware I2C faults cannot be made absolutely fail-safe
by software alone.

Optional `hts stop` aborts further heater cycles, turns heater OFF
and verifies the result. Other `hts` manual diagnostic commands
are not permitted until the automatic sequence has finished;
the user does not need to type any command for the normal workflow.

This **10s pulse is a comparative diagnostic** and is shorter than
the minutes recommended by ST for recovery from prolonged
condensation. Three short pulses do not establish that the heater
can repair a sensing element. Do not interpret absence of recovery
as proof of permanent physical damage.

Authority: ST HTS221 DS Rev 5 Section 7.4 and official ST forum
discussion of heater cycling (Jan 2020).

- [x] On-target three cycles entered HEATING, COOLDOWN and DONE without CLI.
- [x] Log reports heater ON verified for three 10-second intervals and heater OFF readback verified after each.
- [x] HTS221 T/RH withheld throughout heating and cooldown.
- [x] Other sensors continued 1Hz output: 157 logged samples, 156/156 intervals at 1000ms, zero scheduler WARN.
- [x] HTS221 Hraw stays -32768 before, between and after three cycles; RH invalid at 136.94%, factory calibration unchanged.

## 2026-10-28 heater experiment — hardware results and disposition

The actual device completed all three automatically scheduled heater pulses
(10s ON, each followed by verified OFF and 30s cooldown). It logged
157 complete 1Hz sample frames #5..161, and all 156 inter-sample
intervals were **exactly 1000ms**. No `WARN` lines were captured.

| Period | HTS221 raw RH | BME280 RH | Observation |
| --- | --- | --- | --- |
| Initial before heat | -32768 | ~59.0% | INVALID |
| After pulse 1 / 30s cooldown | -32768 | ~58.9% | INVALID |
| After pulse 2 / 30s cooldown | -32768 | ~58.8% | INVALID |
| After pulse 3 / 30s cooldown | -32768 (25 frames) | ~58.5–58.8% | INVALID |

**Thermal coupling evidence:** LPS25H internal temperature rose
from ~25.99°C at preheat to ~26.88°C at first 10s pulse end,
~27.09°C at second, and ~27.17°C at third. BME280 stayed near
26.7–26.8°C. This is evidence of local heat response in the
stacked assembly, not a direct HTS221 heater thermometry measurement.

The three experimental short pulses did not restore HTS221 humidity.
The failure to recover does not by itself prove permanent damage or
invalidate ST's different, much longer condensation recovery guidance.
Repeated automatic heating on every reboot now has no demonstrated
benefit, so the next default firmware is **automatic observation-only
every 5s, Heater disabled**, preserving existing `RH=INVALID` gating,
ordinary 1Hz acquisition, and previously implemented manual diagnosis.
The heater experiment code stays in source but is disabled by
`HTS_AUTO_ENABLE_HEATING=0`.

At the end of the captured text, the host terminal reported
`Error reading from serial device` after sample #161; no accompanying
boot/reset banner establishes RP2350 reset or a firmware fault.
This is unresolved and independent of the completed heater experiment.


## LED matrix default OFF across reset and power-up

A one-shot LED clear already passed real-hardware testing, but a full
power cycle can bring the Sense HAT ATtiny88 online later than RP2350.
The boot routine now clears its 192-byte volatile framebuffer at offsets
0, 50, 150, 350, 750, 1500 and 3000 milliseconds, using the existing
controller ID check (0xF0 = 0x73). This runs before scanning sensors or
starting the console/telemetry loop, and substitutes for the former
3-second CDC wait. Its result controls marquee availability.
The LED will remain OFF unless the user issues a later `led demo` or
`led TEXT` command. **No firmware-only guarantee of a flash-free
instantaneous power-on** is possible while the ATtiny88 contains its
built-in rainbow pattern. New reset and cold-power tests pending.


## 2026-10-28 verification-first HTS221 root-cause gate

**Source implemented; new on-target outcomes are PENDING.** Do not
infer sensor hardware damage while these new Firmware checks are pending.

After `sensor_manager_init()` and before the very first 1Hz sample,
the firmware runs a **single**, bounded, automatically triggered
`HTS221 VERIFY` sequence. No terminal command is needed.

1. Read/print identity, CTRL1/CTRL2, AV_CONF, 16 factory-calibration
   bytes, plus a fresh continuous-mode Hraw/Traw sample.
2. With CTRL1=0x86 and CTRL2=0x00 verified, set
   `CTRL_REG2.BOOT=1` exactly **once per RP2350 boot**.
   Poll up to 100×10ms for bit7 to clear itself. Verify CTRL1/CTRL2/
   AV_CONF restoration. Compare the public calibration bytes before
   and after (NOTE: this does NOT expose internal trim registers).
   Read fresh continuous Hraw/Traw again.
3. Temporarily set AV_CONF=0x1B and CTRL1=0x84. Drain stale H/T
   output registers, trigger ONE_SHOT, independently poll H_DA
   and T_DA for up to 3 seconds. Read the H/T register pairs as
   each individual data-ready flag asserts. A timeout on one does
   **not** erase a success on the other. Print both latencies/raw.
4. Write CTRL2=0x00, CTRL1=0x86, restore the exact original AV_CONF,
   and verify all register readbacks. If this fails, **quarantine**
   HTS221 (no further reads until reset); other sensors remain
   available. Capture post-test continuous output.
5. Print a mechanism summary (BOOT self-clear; ONESHOT H/T ready)
   independently of `RH_VALID` and raw values. A mechanism PASS is
   **not** proof of functional humidity sensing.

The test never writes the calibration bank or turns on the heater.
It is a temporary fault-isolation run, **not** a recommended
normal-production boot sequence. It adds boot time, not a
steady-state 1Hz cycle delay. The script intentionally stays in
HTS221-only console mode for the current investigation.

**What is already backed by target logs:** factory calibration
readout, 7Hz continuous acquisition, burst & single registers,
AVGTEST 0x1B comparison, heater cycles and safety restoration,
and invalid humidity. **What is still not proved:** BOOT
self-clear on this board, RH response after BOOT, independent
One-shot H_DA/T_DA latencies, restoration on the next flash,
true cold-boot vs MCU-only reset, and the host build/flash of
the latest main branch.

No hardware-root-cause declaration is authorized by this document
before those results have been reviewed.
