# RP2350 Environment Monitor

Embedded environmental-sensor bring-up for **Waveshare RP2350-PiZero + Environment Sensor HAT**, directly connected over the 40-pin header.

**Status:** P0 five sensor identities PASS; P1 all-sensor initial data-acquisition PASS on hardware (2026-10-28), with 6/6 INIT OK and nine consecutive 1 Hz USB CDC samples. **Remaining:** SGP40 VOC Index after warmup, LTR390 UV stimulus response, lux/IMU calibration, offline incremental build and long-duration validation. See [P1 all-sensor evidence](docs/p1-all-sensors.md) and [validation record](docs/validation.md).

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
