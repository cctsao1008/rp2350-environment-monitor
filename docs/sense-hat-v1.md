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
