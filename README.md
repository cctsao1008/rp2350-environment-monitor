# RP2350 Environment Monitor

Embedded environmental-sensor bring-up for **Waveshare RP2350-PiZero + Environment Sensor HAT**, directly connected over the 40-pin header.

**Status:** P0 five sensor identities PASS; P1 all-sensor initial data-acquisition PASS on hardware (2026-10-28), with 6/6 INIT OK and nine consecutive 1 Hz USB CDC samples. **Remaining:** SGP40 VOC Index after warmup, LTR390 UV stimulus response, lux/IMU calibration, offline incremental build and long-duration validation. See [P1 all-sensor evidence](docs/p1-all-sensors.md) and [validation record](docs/validation.md).



## Current hardware configuration (2026-10-29)

**Default build: Environment Sensor HAT only.** The Raspberry Pi Sense HAT V1
has been physically removed. Build option `ENABLE_SENSE_HAT_V1` defaults to
`OFF` (and `PROJECT_ENABLE_SENSE_HAT_V1=0`). All Sense HAT V1 drivers are
excluded from the ELF and all corresponding runtime paths are disabled:
no HTS221/LPS25H/LSM9DS1 init, reads, register snapshots, BOOT reloads,
HTS automatic diagnostics, ATtiny88 LED clears, I2C identity scans, or
LED/HTS console polling. No `HTS221 ERROR / NOT_INSTALLED` clutter is printed.
The normal 3-second USB CDC attach window remains, with no Sense HAT I2C.

**Enabled 1 Hz USB CDC stream:** BME280 (T/RH/P), TSL2591 (light),
LTR390 (UV/ALS), SGP40 (SRAW/VOC Index) and ICM20948 including AK09916.
The BME280→SGP40 compensation dependency is unchanged. If one of these
sensors is absent or fails it reports its own `ERROR` status; we do not
claim new hardware verification on this build yet.

To restore Sense HAT V1 **after physically reconnecting it**, configure
CMake with `-DENABLE_SENSE_HAT_V1=ON`, rebuild and flash. For the current
removed-HAT configuration leave it `OFF`. On an existing build directory
that previously had `ON` explicitly set, CMake caches the option: set
`-DENABLE_SENSE_HAT_V1=OFF` while configuring before building.

The prior HTS221 investigation remains documented in
`docs/sense-hat-v1.md` and `docs/validation.md`; it is archived
as prior evidence rather than deleted.

## Experimental USB Web branch (Phase 1 / Phase 2)

The opt-in `feature/usb-ncm-web-phase1` branch uses
`ENABLE_USB_NCM_WEB_PHASE1=ON`; the default build remains the stable
CDC-only configuration, and `main` is unchanged.

**Phase 1 hardware PASS (2026-10-30):** Windows 11 CDC-NCM and CDC ACM,
DHCP, DNS (`sensor.usb`), ICMP, lwIP HTTP, and automatic Vendor Reset /
ELF update through `scripts/flash.ps1 -BuildDir build-ncm -Ncm`.
The user reported the stability run completed; its aggregate metrics
were not supplied. The two-value TinyUSB NCM divisor fix is now
reproduced in the build directory without modifying the pinned submodule.

**Phase 2 source staged (not rebuilt or device-tested):** a read-only
Sensor JSON API at `/api/v1/sensors.json` and an embedded live Dashboard
at `/` use the existing 1Hz Sensor Manager snapshot. The Dashboard now
identifies stale/repeated sequences, reports per-sensor validity, and
plots browser-only five-minute trends for BME280 temperature/RH,
LTR390 illuminance and SGP40 VOC Index. No additional I2C acquisition,
persistent history, or external web dependencies.

See [Phase 1 evidence](docs/usb-ncm-phase1.md) and
[Phase 2 API / implementation notes](docs/usb-web-phase2.md).

## Baseline architecture

- **Target:** `waveshare_rp2350_pizero` (RP2350 Arm; I2C1 on GPIO2/3).
- **SDK:** Pico SDK **2.3.1**, pinned Git submodule at `lib/pico-sdk`.
- **Build:** CMake + Ninja + Arm GCC, with preinstalled host tools.
- **Programming:** `picotool` loading an **ELF** through PICOBOOT; update/verify/execute. No MSC file-copy update path.
- **Console:** USB CDC through Pico SDK stdio.
- **P0 diagnostics:** one boot-time I2C read-probe (diagnostic only) plus all five device-specific identity reads.
- **P1 all-sensor integration (initial data acquisition hardware PASS):** BME280 T/RH/P, TSL2591 lux, LTR390 UV/ALS, SGP40 SRAW_VOC/VOC Index, ICM20948 6-axis plus AK09916 magnetometer, 1 Hz Sensor Manager/USB CDC. USB CDC fixed-point output.

## Clone

```powershell
git clone --recurse-submodules https://github.com/cctsao1008/rp2350-environment-monitor.git
cd rp2350-environment-monitor
```

On an existing checkout:

```powershell
git submodule update --init --recursive
```

## Host setup — install once

Install Git, CMake (>= 3.21), Ninja and Arm GNU Toolchain (arm-none-eabi-gcc) separately. They are **not** downloaded by this repository.

For Windows x64, explicitly install the official *Pico SDK 2.3.1* host tool archives only once:

```powershell
.\scripts\setup-tools.ps1
```

This downloads **picotool 2.3.1** and **pioasm 2.3.1** from Raspberry Pi's official GitHub release assets, validates pinned SHA-256 digests, extracts them under `C:\Toolchains\pico-tools\2.3.1`, and sets the **user-level** `PICO_HOST_TOOLS_ROOT` variable. Existing complete installations are reused. **Normal build and flash scripts never invoke this installer and never download tools.**

To choose another shared location:

```powershell
.\scripts\setup-tools.ps1 -InstallRoot "D:\tools\pico\2.3.1"
```

Open a new PowerShell after installation to load the saved user-level variable. You can also set `$env:PICO_HOST_TOOLS_ROOT` manually when using an existing installation. Tool ZIPs and executable files are not committed to Git.

## Windows workflow

```powershell
.\scripts\build.ps1 -Mode check
.\scripts\build.ps1 -Mode configure
.\scripts\build.ps1
.\scripts\flash.ps1
```

If you already have compatible tools installed elsewhere, set `PICO_HOST_TOOLS_ROOT` (or pass `-ToolsRoot` to the scripts). The build script resolves the executable and the matching CMake package configuration automatically. For installations outside the standard archive layout, pass both `-PicotoolDir` and `-PioasmDir` to `build.ps1`.

```powershell
.\scripts\build.ps1 -Mode configure -ToolsRoot "D:\tools\pico\2.3.1"
```

Subsequent builds reuse `build/` and run incrementally.

Alternatively use standard CMake directly (`picotool_DIR` / `pioasm_DIR` must be discoverable):

```powershell
cmake --preset default
cmake --build --preset default
```

The CMake configuration uses `find_package(... REQUIRED CONFIG)` for both host-side tools, and `FETCHCONTENT_FULLY_DISCONNECTED=ON`. Missing compatible tools fail instead of being downloaded automatically. The script's `check` mode verifies CLI prerequisites and the pinned SDK; CMake package version resolution is verified in `configure`.

## Flashing without MSC

```powershell
.\scripts\flash.ps1
```

Internally this calls `picotool load -u -v -x -f build/environment_monitor.elf`.

- `-f`: request reset from a running **compatible** USB firmware into BOOTSEL.
- `-u`: skip unchanged sectors.
- `-v`: verify programmed contents.
- `-x`: execute the image.

If auto-reset is unavailable, use the board's hardware BOOTSEL entry and run the same flash script. The application-initiated reset config sets `PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1` (MSC disabled, PICOBOOT enabled); a physical BOOTSEL/cold-boot entry may still enumerate MSC. **No firmware update uses MSC**, but this is not a blanket guarantee that MSC can never enumerate. For environments requiring no MSC enumeration under *any* failure path, use SWD/OpenOCD recovery instead.

For multiple devices, set `-Serial <device serial>`; never assume which connected board is selected.

## Project files

```text
lib/pico-sdk/          pinned SDK Git submodule
src/main.c             Startup I2C identity checks + periodic USB CDC output
src/sensor_manager.*   Minimal sensor sampling coordinator
src/sensors/           Five sensor drivers, I2C helpers, fixed-point result APIs
third_party/bosch_bme280/  Pinned Bosch SensorAPI + BSD-3-Clause license
third_party/sensirion_gas_index/ Pinned Sensirion VOC Index algorithm + license
scripts/setup-tools.ps1  explicit one-time tool download + SHA256 verification
scripts/build.ps1      preflight / configure / incremental build
scripts/flash.ps1      ELF flash / verify / reboot using picotool
docs/hardware.md       wiring and interface notes
docs/validation.md     acceptance criteria and test evidence
```

## Follow-up development

P0 Sensor Identification: **PASS (5/5)**. Remaining infrastructure qualification: offline incremental build and automatic runtime-reset flashing (including checking MSC enumeration policy).

P1: All five sensor acquisition paths plus AK09916 are integrated and producing real USB CDC data on hardware; [measurements and remaining validation](docs/p1-all-sensors.md). The older BME280-only 33-sample on-device test remains documented in [BME280 evidence](docs/p1-bme280.md).

P2: timestamped USB CDC output, persistent microSD logging and optional on-device display.

## References

- [RP2350-PiZero](https://www.waveshare.com/wiki/RP2350-PiZero)
- [Environment Sensor HAT](https://www.waveshare.com/wiki/Environment_Sensor_HAT)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [picotool](https://github.com/raspberrypi/picotool)

## License

MIT; see [LICENSE](LICENSE).

## Optional Raspberry Pi Sense HAT V1 LED auto-off

When an original Sense HAT V1 is stacked on the same 40-pin header,
its ATtiny88 controller (I2C `0x46`) displays the firmware's default
rainbow image until explicitly cleared. Our RP2350 firmware now checks the
controller identity register `0xF0` for `'s'`, then writes zeros only to
the 192-byte volatile LED framebuffer (`0x00` through `0xBF`) immediately
after I2C initialization. It performs no EEPROM write and does not require
Raspberry Pi OS, a Linux framebuffer driver, or additional hardware.

A missing Sense HAT or unsupported controller results in
`Sense HAT V1 LED matrix: not detected / clear failed`; the rest of
the environmental sensor acquisition continues as before.

Source references: [Raspberry Pi Sense HAT ATtiny88 firmware](https://github.com/raspberrypi/rpi-sense/blob/master/main.c)
and [LED framebuffer Linux driver](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/video/fbdev/rpisense-fb.c).


## Optional Sense HAT V1 sensor ID validation

When the official Sense HAT V1 is installed, the firmware performs a
**read-only, one-time WHO_AM_I check** (register `0x0F`) on:

| Sensor | I2C address | Expected ID |
| --- | --- | --- |
| LSM9DS1 accelerometer / gyroscope | `0x6A` | `0x68` |
| LSM9DS1 magnetometer | `0x1C` | `0x3D` |
| LPS25H pressure | `0x5C` | `0xBD` |
| HTS221 humidity / temperature | `0x5F` | `0xBC` |

Results are printed as `PASS`, `UNEXPECTED`, or `NO_RESPONSE`.
The Sense HAT sensor identity checks passed on target (4/4). Independent\nHTS221, LPS25H and LSM9DS1 acquisition paths are now implemented, with\nphysical data acquisition validation pending.
The existing Environment HAT sampling continues unchanged, whether the
Sense HAT devices respond or not.

References: [official Sense HAT schematic](https://pip-assets.raspberrypi.com/categories/676-raspberry-pi-sense-hat/documents/RP-008385-DS-1-sense-hat-schematics.pdf);
ST LSM9DS1, LPS25H, HTS221 datasheets.


## Sense HAT V1 measurement support

The Sense HAT V1 identity check is **hardware PASS (4/4)**, and the original
Environment HAT continues to sample. Additional ST measurement drivers for
HTS221, LPS25H and LSM9DS1 have been added in `src/sense_hat_sensors.c`.
Their live data, 1 Hz stability and orientation are **not yet verified**; see
[Sense HAT V1 acquisition validation](docs/sense-hat-v1.md).


## Sense HAT V1 marquee (optional, default OFF)

The official V1 LED matrix can now display **low-brightness 8x8 scrolling
ASCII text**. No LED updates run unless requested from USB CDC. The 64-LED
framebuffer is accessed at I2C address `0x46` only after its controller
identity has been verified; no EEPROM/control-register writes.

With a terminal that supports sending a line to USB CDC:

- `led demo` — scroll HELLO RP2350 once, then blank the matrix.
- `led HELLO WORLD` — scroll custom uppercase/ASCII text (max 48 chars).
- `led b 1` — select low brightness (levels 1..4 of 31).
- `led off` — immediately clear the display.
- `help` — show the command summary.

Default brightness is **2/31 green only**, with a 170ms column period.
LED text is an optional diagnostic display, never a blocking dependency of
sensor sampling. Avoid leaving the LEDs on during thermal accuracy testing.

**Known sensor-quality issue:** The captured initial Sense HAT measurements
show HTS221 humidity stuck at clipped 100% vs BME280 around 62%.
Firmware now exposes raw HTS221 humidity ADC and unclamped interpolation on
invalid measurements, plus factory coefficients at startup, so the issue can
be diagnosed without assuming the physical RH is 100%.


### Latest hardware findings (2026-10-28)

- Sense HAT V1 LED marquee: **functional PASS** (user on-device test).
- HTS221 humidity: **INVALID**, constant raw -32768 / interpolated
  ~136.94%RH (BME280 ~58.7%RH); diagnostic registers now compared using
  individual byte reads to distinguish software burst-read issues from
  sensor/calibration faults. Do not use for sensor fusion.
- The LED-driven loop's prior 5ms polling sleep caused repeated 2..3ms
  sample deadline warnings; precise deadline scheduling is now implemented
  in source, awaiting target verification.


## HTS221 fault-isolation commands (manual, Sense HAT V1)

The Sense HAT V1 HTS221 humidity channel has repeatedly returned the exact
signed 16-bit minimum `0x8000` (reported as -32768), while its temperature
channel updates and the BME280 reports RH around 59..62%. The ST datasheet
does not define `0x8000` as an error code, so it must **not** be treated as
proof of damage, nor used as valid humidity.

With USB CDC running, type:

- `hts diag`: read-only snapshot of HTS221 ID, CTRL_REG1/2, AV_CONF,
  factory calibration bytes, STATUS, and burst versus individual 16-bit
  output registers; leaves sensor configuration untouched.
- `hts oneshot`: opt-in alternative conversion path. Only runs if the
  device identity and control registers match the known active
  7Hz/heater-off settings. Temporarily switches to one-shot, waits for
  fresh humidity and temperature data, reports raw results, and restores
  continuous 7Hz mode. In a manual diagnostic session this may temporarily
  alter the current 1Hz sample scheduling.
- `help`: prints both LED and HTS221 commands.

Avoid issuing `hts oneshot` during timing qualification. If mode
restoration cannot be confirmed, HTS221 reads are disabled until reset.
No calibration registers, flash trim / BOOT or heater commands are written.

See [HTS221 diagnostic acceptance](docs/sense-hat-v1.md). **Source
implementation only until on-target testing confirms it.**


### New HTS221 controlled averaging comparison

Hardware diagnostic exposed `AV_CONF=0x3F` (max averages: 256 T / 512 RH)
versus factory power-on default `0x1B` (16 T / 32 RH), along with repeated
invalid humidity 0x8000 and one outlying 0x4065. One-shot at 0x3F sometimes
timed out because T_DA did not assert within the diagnostic window.

Use `hts avgtest` for an explicit, reversible 7Hz continuous mode test.
The command temporarily sets **only** `AV_CONF=0x1B`, reads eight new
samples at 200ms spacing, and restores the original value with readback.
This manual test can delay the next 1Hz sample; it never runs on boot.
Try a complete power disconnect first and inspect `hts diag` for the
factory reset `AV_CONF=0x1B` before any mode/configuration experiments.


### Complete HTS221 register dump

Enter `hts dump` over USB CDC to print all **documented** control,
status, output and factory-calibration registers (hex with addresses),
plus decoded H/T raw ADC, factory calibration, unclamped RH and validity.
This is read-only and does not modify HTS221 settings; reading the high
byte of the output pair clears the corresponding data-ready status flag
as documented in the datasheet. Reserved addresses outside the
0x30..0x3F calibration block are skipped. The feature is currently
source-implemented and pending RP2350 hardware testing.


### HTS221 automatic heater diagnostic

No terminal commands required. After HTS221 initialization, Firmware
automatically performs a **finite** three-cycle heater comparison:
5s baseline, 10s heater ON, 30s cooldown per cycle. Every 5s it emits
an `HTS221 AUTO` status line (including cached raw values when not
heating/cooling). Stop heating early if RH becomes valid after cooldown. Once finished,
logging continues every 5s with heater OFF. Existing environmental measurements stay on their 1Hz schedule,
except HTS221 T/RH are withheld during heater and cooldown. This is a
short-pulse diagnostic and is **not** the minutes-long ST-recommended
condensation recovery procedure.

An optional `hts stop` command forces heater OFF and aborts additional
cycles. If OFF cannot be verified due to a failed I2C transaction,
firmware keeps retrying and warns that disconnecting power may be
necessary. A hardware fault cannot be guaranteed safe through FW alone.


### 2026-10-28 heater experiment result: default is observation-only

The earlier automatic three-cycle heater experiment **finished successfully**
on target, but humidity stayed `raw=-32768` and `RH=INVALID` before
and after all three pulses. Repeating automatic heating on each reset
adds heat and no demonstrated measurement benefit. Consequently the
**latest firmware defaults to no automatic heater operation**.

It still logs the last HTS221 raw T/RH and validity every **5s**
without terminal commands, using a `phase=OBSERVE_ONLY` status line;
the ordinary 1Hz sensor records continue. The heater test's source remains
available behind the compile-time `HTS_AUTO_ENABLE_HEATING` setting,
default `0`. Reenabling it is an explicit engineering change, not a
required runtime command. See `docs/sense-hat-v1.md`.


### Sense HAT V1 startup LED policy

The LED matrix is **OFF by default after each RP2350 reset or full
power-on**. Early boot makes seven controller-ID-checked clear attempts
across the existing ~3-second USB CDC startup window, including a final
clear after the ATtiny88 should have finished initializing. This
addresses delayed startup of the ATtiny88 as well as RP2350-only reset
with an old LED framebuffer. No special USB CDC command is needed.
The `led demo` and manual text marquee remain available afterwards.

A brief flash of the ATtiny88's built-in default image during true cold
power-up cannot be ruled out with RP2350 firmware alone; strict
power-on darkness would require ATtiny firmware or LED power control.


### Temporary HTS221-only USB CDC output (2026-10-28)

For HTS221 raw-humidity fault isolation, `src/main.c` defines
`HTS221_ONLY_OUTPUT=1`. At boot, it prints the HTS221 initialization
status but suppresses I2C scan, the four-device Sense HAT identity report,
and initialization-status text for other sensors. During normal operation,
only `[sample=... uptime=...]` and HTS221 T/RH with raw/unclamped
invalid humidity appear each second. The HTS221 auto-observation state
still reports cached raw values and validity every 5s. All other
sensor drivers remain enabled, initialized, and sampled at 1Hz;
**BME280 temperature/RH continue compensating SGP40 internally**.
This is a **console output filter**, not a sensor power-down or a change
to the shared I2C bus. LED startup blanking remains enabled. Set
`HTS221_ONLY_OUTPUT=0` to restore the prior full sensor telemetry.


### 2026-10-28 HTS221 humidity observation update

**Newer evidence supersedes earlier "persistently invalid" hardware
status:** after external hair-dryer warm air, HTS221 humidity raw moved
from `0x8000 (-32768)` to around `-7616..-7683`, and the
unchanged factory calibration yielded ~54.5–54.8%RH with
`RH_VALID=YES`. The chip remained hot (~54–57°C). This demonstrates
that the raw output is **not irreversibly stuck**, but does **not**
yet prove humidity accuracy at room temperature. ST describes the
heater's role in condensation recovery. See
`docs/validation.md` for the remaining ambient cool-down,
comparison and power-cycle tests. Do not overwrite factory trim
or force a software offset solely from the heated readings.
