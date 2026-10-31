# Browser-based binary log export — experimental, opt-in

**Implementation branch:** `feature/binary-log-web-download` (based on `feature/binary-flash-logger`).
Host CI: run 41230405750 **PASS** (native HTTPD, codec, Python parser,
browser script and simulated end-to-end download/race tests).

**Security prerequisite: Cloudflare Access is NOT enabled in the currently connected
Cloudflare account (Cloudflare API returned `access.api.error.not_enabled`).
Do not turn on `ENABLE_BINARY_WEB_EXPORT` while
`sensor.cafefeed.idv.tw` remains accessible to unauthenticated visitors.**
The firmware does **not** independently authenticate visitors; its access
control is provided by Cloudflare Access at the public ingress. Local
physical USB access to `192.168.7.1` remains possible.

The existing live Dashboard, sensor JSON endpoint and 1 Hz flash logger are
unchanged when the export feature is disabled (default).

## Build gates

```text
ENABLE_USB_NCM_WEB_PHASE1=ON
ENABLE_BINARY_FLASH_LOGGER=ON
ENABLE_BINARY_WEB_EXPORT=OFF    # DEFAULT: protect the public tunnel
```

Only after Cloudflare Access protection is configured and verified:

```powershell
cmake -S . -B build-ncm `
  -DENABLE_USB_NCM_WEB_PHASE1=ON `
  -DENABLE_BINARY_FLASH_LOGGER=ON `
  -DENABLE_BINARY_WEB_EXPORT=ON
.\scripts\build.ps1 -BuildDir build-ncm
```

This is source-only and unqualified for on-device browser download until
the user actually builds/flashes and tests it. Do **not** flash just because
the native host CI passes.

## Cloudflare Access checklist (mandatory before enabled deployment)

1. In Cloudflare Zero Trust, **enable Access** for the account and
   configure an identity provider and an Allow policy restricted to the
   intended owner. Never create an Allow Everyone rule for log exports.
2. Protect **all** HTTP routes, not merely the visible button:
   - `sensor.cafefeed.idv.tw/logs.html`
   - `sensor.cafefeed.idv.tw/api/v1/log/status`
   - `sensor.cafefeed.idv.tw/api/v1/log/list*`
   - `sensor.cafefeed.idv.tw/api/v1/log/sector*`
   Path patterns and applications should be checked against the Access
   configuration actually applied; **a Tunnel itself is not authentication**.
3. From an unauthenticated browser/private window, verify each protected
   path prompts for Access login or denies the request. Also test a
   direct URL to the binary sector endpoint, not just `logs.html`.
4. Authenticate as an allowed user and verify that `logs.html` and the
   JSON/binary export APIs work. Keep `/api/v1/sensors.json` public
   only if that is intentionally the established policy.
5. The microcontroller does not verify Cloudflare Access JWTs;
   any alternate bypass route, direct LAN exposure, or disabled Access
   policy can bypass edge authentication.

If Access cannot be configured, **leave the feature OFF** and continue
using the already-proven USB Vendor Reset + `picotool save -r` path.

## HTTP API (GET, no arbitrary-address reads)

| URL | Payload |
| --- | --- |
| `/logs.html` | Download interface accessible after Access authentication |
| `/api/v1/log/status` | Bounded JSON summary and export geometry |
| `/api/v1/log/list?start=0` | Manifest entries for 64 physical sectors, next start is 64 |
| `/api/v1/log/sector?slot=N&gen=G&crc=C` | Exactly 4096 bytes of that known ELOG sector |

- `slot`: 0..3071, mapped internally to flash offset
  `0x400000 + slot * 4096`. **No user-controllable address.**
- `gen` and `crc` come from the manifest and are checked against a
  valid ELOG Sector Header before the 4096-byte block is copied.
- If a sector was overwritten between manifest and transfer, HTTP
  **409 Conflict** aborts the download, rather than silently mixing
  generations.
- HTTPD uses a single static `4096 + 256` byte response buffer in
  RP2350 SRAM; concurrent exports receive **503 Retry-After: 1**.
  A sector is copied into SRAM before HTTP transmission, preventing
  in-flight Flash erases/programs from changing the response bytes.
- `Cache-Control: no-store` for all APIs. No POST, filesystem,
  arbitrary Flash dumps or firmware modifications exposed.
- The client fetches manifest pages serially, reconstructs the familiar
  **12 MiB raw Flash partition image** with missing sectors set to 0xff,
  and checks each Sector Header CRC in the browser. The host parser
  continues to be responsible for per-Page CRC validation.
- Each download **re-reads status at click time** (the browser tab might
  have been left open for hours). Before releasing the file, it re-reads
  all manifest pages and aborts if the sector generations/CRCs or total
  sector count changed during transfer. This does not freeze actively
  accumulating pages, but detects ring recycling and sector additions.
- Downloads do **not** stop the logger or reboot RP2350. While the
  browser is enumerating all 3072 slots, new sectors may be added; the
  browser aborts if the manifest count differs from the initial status.
  For a full ring, a very long download may race sector recycling and
  fail with 409; the user can retry. This is intentional fail-closed
  behavior and not a multi-second atomic snapshot.
- Full 12 MiB binary export into browser RAM (~12 MiB) can be expensive
  over the USB-NCM/Tunnel path. Performance and USB responsiveness
  require actual hardware tests.

## Downloaded file compatibility

The downloaded `.bin` is exactly 12,582,912 bytes, preserving physical
sector positions and empty bytes as 0xFF. It is directly compatible with:

```powershell
py .\tools\envlog_parse.py .\environment-log-*.bin --csv .\envlog.csv
```

Use a concrete filename if PowerShell wildcards are not expanded by Python.

## Validation gates

- [x] Native C export server test for bounded address and generation,
  request lock, HTTP status/list/sector, malformed query
- [x] Browser JS syntax test in CI
- [x] Full existing Dashboard HTTPD native syntax check with export ON
- [x] Browser automation-style Node contract test: synthetic 12 MiB
      download and second-pass generation-race rejection
- [ ] ARM Windows build with `ENABLE_BINARY_WEB_EXPORT=ON`
- [ ] Existing 1 Hz JSON Dashboard regression
- [ ] Actual localhost USB CDC-NCM browser download
- [ ] Existing public Cloudflare domain with Access enabled and tested
- [ ] On-device concurrent logging + downloadable data CRC tests
- [ ] Full 12 MiB export latency and 1 Hz sampling effect

**Never assume Cloudflare Access is enabled based on a successful firmware build.**
