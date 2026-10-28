# RP2350 Environment Monitor

Embedded environmental-sensor bring-up for **Waveshare RP2350-PiZero + Environment Sensor HAT**, directly connected over the 40-pin header.

**Status:** Initial firmware scaffold only. Hardware, flashing and sensor readings have **not** yet been verified on a physical board.

## Baseline architecture

- **Target:** `waveshare_rp2350_pizero` (RP2350 Arm; I2C1 on GPIO2/3).
- **SDK:** Pico SDK **2.3.1**, pinned Git submodule at `lib/pico-sdk`.
- **Build:** CMake + Ninja + Arm GCC, with preinstalled host tools.
- **Programming:** `picotool` loading an **ELF** through PICOBOOT; update/verify/execute. No MSC file-copy update path.
- **Console:** USB CDC through Pico SDK stdio.
- **P0 firmware:** periodic I2C probe and BME280 chip-ID check.

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

Install Git, CMake (>= 3.21), Ninja, Arm GNU Toolchain (`arm-none-eabi-gcc`), picotool (>= 2.3.0) and pioasm **2.3.1**. They should be reusable across RP2350 projects, not downloaded separately for each build.

The `picotool` and `pioasm` command-line tools must be in `PATH`. CMake additionally requires their **package config directories**, containing `picotoolConfig.cmake` and `pioasmConfig.cmake`. Set `picotool_DIR` and `pioasm_DIR` in your environment, supply them as script parameters, or use a system-visible `CMAKE_PREFIX_PATH`.

Official prebuilt tools: https://github.com/raspberrypi/pico-sdk-tools/releases

## Windows workflow

```powershell
.\scripts\build.ps1 -Mode check
.\scripts\build.ps1 -Mode configure
.\scripts\build.ps1
.\scripts\flash.ps1
```

If CMake cannot locate installed host tool packages, supply their actual CMake config directories:

```powershell
.\scripts\build.ps1 -Mode configure -PicotoolDir "D:\tools\picotool\lib\cmake\picotool" -PioasmDir "D:\tools\pioasm\lib\cmake\pioasm"
```

These paths are examples only. Do **not** point a `*_DIR` variable at an EXE file. Subsequent builds reuse `build/` and run incrementally.

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
scripts/build.ps1      preflight / configure / incremental build
scripts/flash.ps1      ELF flash / verify / reboot using picotool
docs/hardware.md       wiring and interface notes
docs/validation.md     acceptance criteria and test evidence
```

## Follow-up development

P0: prove offline build and PICOBOOT flash on hardware; verify BME280 and the I2C bus.

P1: integrate TSL25911, LTR390, SGP40 and ICM20948 drivers; implement data collection and VOC Index.

P2: timestamped USB CDC output, persistent microSD logging and optional on-device display.

## References

- [RP2350-PiZero](https://www.waveshare.com/wiki/RP2350-PiZero)
- [Environment Sensor HAT](https://www.waveshare.com/wiki/Environment_Sensor_HAT)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [picotool](https://github.com/raspberrypi/picotool)

## License

MIT; see [LICENSE](LICENSE).
