# USB Sensor Web — Phase 2 (implementation staged)

Status (2026-10-30): **Source committed, NOT rebuilt or hardware-tested.**
The user requested no further test cycles. This is a code/contract record,
**not** a new hardware validation claim.

## Scope

Phase 2 reuses the proven Windows 11 USB CDC-NCM network and cooperative lwIP
HTTPD. No Wi-Fi, extra Ethernet hardware, second network task, I2C read
from HTTP, or Sensor Manager refactoring.

- Dashboard: `http://192.168.7.1/` or `http://sensor.usb/`
- Equivalent page: `/index.html`
- JSON API: `http://sensor.usb/api/v1/sensors.json`
- HTTP GET only; no device-write/control endpoints, no HTTPS/authentication.
- Entire Dashboard is embedded; no external CSS, JS, or Internet access.
- Browser polls approximately each second, not a stream.

Existing `ENABLE_USB_NCM_WEB_PHASE1=ON` remains the opt-in for the experimental
Composite/NCM firmware including Phase 2. Stable CDC-only is still the
default (`OFF`). The `main` branch is unchanged.

## Data ownership and memory

`sensor_manager_sample()` produces one 1Hz `sensor_snapshot_t` as before.
`main.c` calls `sensor_web_publish_snapshot(&reading)` only after completion.
The web module copies the complete snapshot. HTTP never touches the I2C
bus or initiates an extra acquisition. The cooperative network loop still
pauses during the existing blocking I2C transactions.

Each API GET allocates its own bounded 2048-byte JSON response in lwIP
heap and releases it in `fs_close_custom()`, rather than sharing a mutable
response buffer between clients. HTTP 503 JSON is returned before the first
sample or on allocation/encoding failure.

## JSON contract

`GET /api/v1/sensors.json` returns HTTP 200 and
`Content-Type: application/json` with `Cache-Control: no-store`.

| Field | Meaning |
| --- | --- |
| `schema` | String `rp2350.environment.v1` |
| `sequence`, `uptime_ms` | Sensor Manager count / milliseconds since boot |
| `bme280` | `ok`, temperature 0.01 °C, RH 0.01%, pressure in Pa |
| `tsl2591` | `ok`, saturation, raw counts, estimated `lux_centi` when valid |
| `ltr390` | `ok`, UV/ALS raw counts, estimated `uvi_centi` and `lux_centi` |
| `sgp40` | `ok`, `sraw_voc`, `index_ready`, `voc_index` after warmup only |
| `icm20948` | `ok`, `accel_mg[3]` (0.001g), `gyro_mdps[3]` (0.001°/s) |
| `ak09916` | `ok`, `mag_centi_ut[3]` (0.01µT), `calibrated:false` |

When `ok` is false, numeric fields are **omitted**, not represented as
valid zero. When TSL2591 saturates, estimated lux is omitted.
SGP40 VOC Index is **not ppm**. AK09916 values are **uncalibrated**.
Dashboard converts fixed-point quantities for human display.

## Implementation

- `src/usb_composite/sensor_web.c`: HTML, JSON, HTTPD custom file callbacks.
- `src/usb_composite/sensor_web.h`: latest-snapshot publish interface.
- `src/main.c`: publish after each completed 1Hz acquisition.
- `src/usb_composite/lwipopts.h`: custom HTTP files and bounded lwIP heap.
- `src/usb_composite/usb_ncm_network.c`: same HTTPD instance.
- `CMakeLists.txt`: Phase 2 code compiled only with NCM opt-in.

## Reproducible TinyUSB divisor fix

At configure time, the project reads the pinned TinyUSB
`src/class/net/ncm_device.c`, checks the expected assignments, and writes
a generated driver in the **build directory** with `wNdbInDivisor=4` and
`wNdbOutDivisor=4`. For this application CMake skips compilation of the
original NCM translation unit and compiles the generated copy instead.
No SDK or nested TinyUSB source files are edited by the build.

A pre-existing, manually modified nested submodule stays dirty on the
user's local machine until explicitly restored. The branch now **does not
depend on** that modification. Earlier hardware NCM PASS used the exact
two-value change directly in the local TinyUSB working tree.

## Qualification boundary

Phase 1 hardware evidence supports NCM, CDC, DHCP, DNS, ICMP, stock HTTP
and `flash.ps1 -Ncm`. The user reported completing the long stability
run without sharing a summary; no numerical pass/fail data are inferred.

Phase 2 JSON, Dashboard and clean-checkout CMake override are
**committed but not rebuilt or target-validated**, by explicit request
for no further tests. This work must not be described as hardware PASS
or merged into stable `main` on an unsupported claim.
