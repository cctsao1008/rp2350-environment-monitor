# RP2350 Environment Monitor

Embedded environmental-sensor bring-up for **Waveshare RP2350-PiZero + Environment Sensor HAT**, directly connected over the 40-pin header.

**Status:** P0 partial hardware validation (2026-10-28). ELF build, PICOBOOT flash/verify, USB CDC, and BME280 chip ID have passed. Other sensors, offline incremental build, and automatic runtime reset remain unverified; see [validation record](docs/validation.md).

## Baseline architecture

- **Target:** `waveshare_rp2350_pizero` (RP2350 Arm; I2C1 on GPIO2/3).
- **SDK:** Pico SDK **2.3.1**, pinned Git submodule at `lib/pico-sdk`.
- **Build:** CMake + Ninja + Arm GCC, with preinstalled host tools.
- **Programming:** `picotool` loading an **ELF** through PICOBOOT; update/verify/execute. No MSC file-copy update path.
- **Console:** USB CDC through Pico SDK stdio.
- **P0 firmware:** periodic I2C read-probe (diagnostic only), BME280 chip-ID check, and SGP40 serial-number command with CRC-8 validation.

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
src/main.c             USB CDC + I2C probing + BME280 chip ID
scripts/setup-tools.ps1  explicit one-time tool download + SHA256 verification
scripts/build.ps1      preflight / configure / incremental build
scripts/flash.ps1      ELF flash / verify / reboot using picotool
docs/hardware.md       wiring and interface notes
docs/validation.md     acceptance criteria and test evidence
```

## Follow-up development

P0: complete the remaining hardware validation, including SGP40 serial-number CRC, offline incremental build, and automatic runtime-reset flashing. BME280 chip ID and PICOBOOT flash/verify have passed.

P1: integrate TSL25911, LTR390, SGP40 and ICM20948 drivers; implement data collection and VOC Index.

P2: timestamped USB CDC output, persistent microSD logging and optional on-device display.

## References

- [RP2350-PiZero](https://www.waveshare.com/wiki/RP2350-PiZero)
- [Environment Sensor HAT](https://www.waveshare.com/wiki/Environment_Sensor_HAT)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [picotool](https://github.com/raspberrypi/picotool)

## License

MIT; see [LICENSE](LICENSE).
