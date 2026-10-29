# USB CDC-NCM Web Phase 1 — Experimental Bring-up

Status: **Windows 11 USB CDC-NCM / CDC ACM, DHCP, DNS, ICMP, HTTP and vendor-reset hardware bring-up PASS** (2026-10-30). Ten-minute concurrency, rollback, replug and clean-checkout build remain PENDING.
This work is confined to `feature/usb-ncm-web-phase1`. The default
`ENABLE_USB_NCM_WEB_PHASE1=OFF` uses the previous firmware path unchanged.

## Interface design

| Interface | Number | USB endpoints | Driver/role |
| --- | ---: | --- | --- |
| CDC-NCM control/data | 0 / 1 | 0x81 notification, 0x02 OUT, 0x82 IN | Windows NCM / USB Ethernet |
| CDC ACM control/data | 2 / 3 | 0x83 notification, 0x04 OUT, 0x84 IN | USB terminal debug |
| Raspberry Pi vendor reset | 4 | control endpoint only | `picotool load -f` auto-BOOTSEL request |

- TinyUSB and lwIP come from **the pinned Pico SDK 2.3.1 submodules**, not from downloaded remote branches.
- One BOS and Microsoft OS 2.0 descriptor set includes the NCM
  **WINNCM** function and the `RPI_RESET_MS_OS_20_DESCRIPTOR(4)`
  WINUSB subset. The Pico SDK reset library is linked separately;
  its own competing MS OS descriptor callback is disabled.
- Uses a development-only test VID/PID `CAFE:40A3` to avoid sharing
  Windows driver cache with the legacy CDC-only image. **Do not ship
  a product with the unregistered VID.**
- The original 6-channel 1Hz Sensor Manager remains; the NCM/lwIP
  stack is serviced cooperatively in the idle portion of the loop.
- Host USB subnet: device `192.168.7.1/24`, DHCP pool
  `192.168.7.2..4`. DNS replies for `sensor.usb`. lwIP
  serves its *stock example diagnostic HTTP page* on port 80.
  **Sensor JSON API and dashboard are not in Phase 1.**
- Neither USB Ethernet nor the stock HTTP page requires upstream
  Internet access, Wi-Fi or an Ethernet PHY.
- CDC logs use a lightweight Pico stdio registration over TinyUSB
  CDC ACM; data is discarded when COM is closed or the FIFO fills.
  It is *not* a reliable logging transport.
- The `pico_usb_reset` Vendor Reset interface was verified on hardware
  with explicit `CAFE:40A3` selection. `picotool` 2.3.1 does not find
  the experimental application without this selection; ROM BOOTSEL
  uses its own VID/PID after reset.

## Windows 11 build (explicit opt-in)

Use a **separate build directory** to keep the stable firmware
configuration and ELF intact. Run in the repository root:

```powershell
git fetch origin
git switch feature/usb-ncm-web-phase1
git submodule update --init --recursive

# Reuse the existing toolchain/package locations established by setup-tools.ps1.
.\scripts\build.ps1 -Mode configure -BuildDir build-ncm
cmake -S . -B build-ncm -DENABLE_USB_NCM_WEB_PHASE1=ON -DENABLE_SENSE_HAT_V1=OFF
.\scripts\build.ps1 -BuildDir build-ncm
```

**Do not flash until the build actually succeeds.**
Keep a copy of the last known-good `build/environment_monitor.elf`.
After building, initially use physical BOOTSEL to enter ROM recovery
and flash the experimental ELF via `picotool` if necessary.

```powershell
# Experimental CDC-NCM application currently running:
.\scripts\flash.ps1 -BuildDir build-ncm -Ncm

# Device already in ROM BOOTSEL (or use physical BOOTSEL):
.\scripts\flash.ps1 -BuildDir build-ncm -NoForce
```

The `-Ncm` opt-in passes the tested `picotool 2.3.1` invocation with
`--vid 51966 --pid 16547 -f` **after the ELF filename**. The ordinary
CDC-only `flash.ps1` behavior is unchanged. The direct `picotool` invocation
has passed on hardware; the new wrapper `-Ncm` option still needs a
separate target run before it is marked validated.

Validated direct command (close Miniterm first):

```powershell
picotool load -u -v -x .\build-ncm\environment_monitor.elf --vid 51966 --pid 16547 -f
```

Do not add the application VID/PID filter to a command targeting a device
*already* in ROM BOOTSEL. For that case use `-NoForce`.

## Windows CDC ACM terminal (Ctrl+X exit)

On Windows 11, use pySerial Miniterm with an explicit exit character:

```powershell
py -m serial.tools.miniterm COM3 115200 --exit-char 24
```

- Exit with **Ctrl+X** (ASCII control code 24 / 0x18). This avoids
  keyboard-layout/IME issues with Miniterm's default exit shortcut.
- Use the actual enumerated COM port; `COM3` is the current test example,
  not a fixed device property.
- Close Miniterm using Ctrl+X before running `scripts/flash.ps1`,
  unplugging USB, or deliberately triggering a vendor reset.
- Do not depend on Miniterm's default exit key in project procedures,
  examples, or validation instructions. This is a host-terminal
  setting only; it does not change the RP2350 firmware or USB descriptors.

## Required target acceptance

- [x] `build-ncm/environment_monitor.elf` cross-compiles and links with pinned Pico SDK 2.3.1, ARM GNU 15.2 and Ninja. User's Windows console reports `[44/44] Linking CXX executable environment_monitor.elf` followed by `[PASS] Firmware ELF:`. SHA-256 of ELF has not been recorded.
- [x] Windows enumerates CDC ACM COM3, NCM, Vendor Reset and USB Composite
      (`CAFE:40A3`), all with PnP status OK.
- [x] Device's HTTP endpoint at `http://192.168.7.1/` serves the
      **lwIP sample page** (HTTP 200; dashboard remains out of scope).
- [x] `ping 192.168.7.1` succeeds (4/4, 1ms).
- [x] DNS resolves `sensor.usb` to `192.168.7.1`; hostname HTTP returns 200.
- [x] USB CDC prints all six sensor channels; observed continuous sequence
      1575..1604 at a mean interval of 1000ms.
- [x] Preliminary concurrent HTTP testing: 17/17 HTTP 200 with active
      sensor CDC output. This is NOT the 30-request or 10-minute acceptance run.
- [ ] NCM traffic does not break 1Hz sampling or SGP40 indexing;
      run at least 10 minutes with page reload and network traffic.
- [x] **Vendor reset**: with the experimental application running,
      `picotool load -u -v -x build-ncm/environment_monitor.elf
      --vid 51966 --pid 16547 -f` enters ROM PICOBOOT without
      physical BOOTSEL, verifies 100%, and requests application reboot.
      The two-stage `picotool reboot -u --vid 51966 --pid 16547 -f`
      then `flash.ps1 -NoForce` also passed. BOOTSEL PICOBOOT works
      without relying on USB mass-storage drag/drop.
- [x] `flash.ps1 -BuildDir build-ncm -Ncm` wrapper on-target PASS:
      automatic BOOTSEL, ELF load 100%, verify OK and reboot request;
      Windows CDC ACM, NCM, vendor reset all re-enumerated with
      PnP status OK, and `http://192.168.7.1/index.html` returned 200.
- [ ] Rollback test from physical BOOTSEL and stable ELF.
- [ ] Windows connection teardown/replug, reset, and serial stability.

Vendor Reset is functional with the explicit experimental VID/PID.
The default `flash.ps1 -BuildDir build-ncm` previously failed because
`picotool` did not discover the experimental VID/PID; do not treat
that as a device-side reset failure. Do not merge this branch before
remaining acceptance work and reproducible dependency patching.

## Known engineering constraints

1. Cooperative network service is paused during blocking I2C
   acquisition (~hundreds of ms each 1Hz cycle). Throughput and USB
   latency require target measurement; eventual web UI may justify
   non-blocking acquisition or a task scheduler.
2. lwIP DHCP, DNS and HTTP are development-only and unauthenticated.
   Restrict to the directly attached host's USB segment; management
   POST/PUT endpoints are deliberately absent.
3. Disable Windows Internet Connection Sharing and avoid routing this
   device onto external networks during experimental testing.
4. For now the CMake option conflicts with `ENABLE_SENSE_HAT_V1=ON`
   to keep the removed Sense HAT out of the experimental test matrix.

## Sources

- Raspberry Pi Pico SDK `pico_usb_reset` (SDK 2.3.1) and its
  custom TinyUSB descriptor instructions.
- Pinned TinyUSB `examples/device/net_lwip_webserver` and
  `lib/networking/dhserver.c`, `dnserver.c`, MIT licensed.
- Pico SDK's pinned lwIP library and built-in HTTP server.

## 2026-10-29 Windows cross-build evidence

The user ran `git pull --ff-only` followed by
`./scripts/build.ps1 -BuildDir build-ncm` on the feature branch.
The build reported ARM GCC 15.2, Pico SDK submodule
`079c6f39023649b154152db30f1d781e884879bc`, and
`[44/44] Linking CXX executable environment_monitor.elf`.
It ended with `[PASS] Firmware ELF: D:\\my-github\\rp2350-environment-monitor\\build-ncm\\environment_monitor.elf`.
This is user-observed **compilation/link PASS** only. There is no
flashing, Windows CDC-NCM, DHCP, HTTP, serial enumeration, or
application-to-ROM Vendor Reset result in the submitted evidence.
Keep `main` unchanged until the experiments pass.

## 2026-10-30 hardware evidence and dependency note

- Windows NCM previously failed with PnP Code 10. Modifying the pinned
  TinyUSB `src/class/net/ncm_device.c` values `wNdbInDivisor` and
  `wNdbOutDivisor` from 1 to 4 fixed NCM enumeration in the observed
  build. This is currently **a dirty nested TinyUSB submodule**, not a
  reproducible repository patch. Preserve it until a pinned patch or
  upstream dependency update is committed and tested.
- The NCM adapter received DHCP address `192.168.7.2/24` from
  `192.168.7.1`; DNS `sensor.usb` and HTTP via hostname PASS.
- The observed HTTP concurrency run finished at 17/17 PASS, without
  reaching the planned 30 requests. CDC sample sequence 1575..1604
  remained consecutive and on a 1Hz schedule in the supplied log.
- A separate direct `picotool load` invocation with test VID/PID **after**
  the ELF file successfully triggered ROM PICOBOOT, loaded and verified
  firmware, and requested reboot. The wrapper `-Ncm` switch was added
  afterward and subsequently passed on-target testing.
