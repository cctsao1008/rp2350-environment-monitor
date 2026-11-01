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

## lwIP URI handling: fix for first download HTTP 400 (2026-11-01)

In a real RP2350 deployment, Cloudflare Access now allows the authorized
owner to load `/logs.html` and the Web UI retrieves `status` with
**359 valid ELOG sector headers**, 0 unknown headers. However the first
browser manifest fetch to `/api/v1/log/list?start=0` returns HTTP 400.

Root cause confirmed against the upstream lwIP HTTPD
`http_find_file()`: it finds `?` and writes NUL into the URI **before**
calling `fs_open()` and the project's `fs_open_custom()`. The project's
earlier direct C host tests bypassed this normalization step, passing
the unsplit query to `log_web_open()`, and therefore missed the bug.

**Fix:** use **query-free path segments** for all required numeric arguments:

```text
GET /api/v1/log/status
GET /api/v1/log/list/0
GET /api/v1/log/list/64
GET /api/v1/log/sector/0/17/4096290011   # values illustrative
```

`N` is the physical list-page start, 0..3071 in multiples of 64.
The sector endpoint takes decimal physical slot, generation, CRC32.
A strict decimal parser rejects overflow, signs, extra fields, empty
segments and out-of-range sector IDs. Existing Cloudflare Access
`/api/v1/log/*` path protection still covers the new endpoints.

Host CI now exercises the exact URI visible *after lwIP normalization*,
not only a raw `?start=` string, and the browser Node test rejects
old query-style requests. This change is entirely HTTP API routing;
the binary format, Flash logger data and sensor sampling are unchanged.

**Target verification pending:** Windows ARM build, USB Vendor Reset
Flash, first local `/api/v1/log/list/0` HTTP 200 returning a JSON
manifest, then authenticated browser download and Parser CRC checks.
Do not interpret the screenshot of HTTP 400 as Flash corruption.

## HTTP API (GET, no arbitrary-address reads)

| URL | Payload |
| --- | --- |
| `/logs.html` | Download interface accessible after Access authentication |
| `/api/v1/log/status` | Bounded JSON summary and export geometry |
| `/api/v1/log/list/0` | Manifest entries for 64 physical sectors, next start is 64 |
| `/api/v1/log/sector/N/G/C` | Exactly 4096 bytes of that known ELOG sector |

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
- Each download **re-reads status at click time** (a tab might have
  been open for hours), then gets a manifest of known sector identities.
  Before releasing the image, it re-scans the manifests and requires
  **all initial sector generation IDs and header CRC32 values to remain
  unchanged**. If circular overwrite replaces an initial sector,
  the transfer aborts instead of silently combining incompatible data.
- Downloads do **not** stop the logger or reboot RP2350. New sectors
  appended while downloading are acceptable and may be absent from this
  export; they must **not** force an abort solely because the valid-sector
  count increased. The current active sector's newly appended pages
  may or may not be included depending on its read time. This is an
  internally CRC-protected *incremental capture*, not an atomic point-in-time
  snapshot, and final per-page CRC validation remains the Host parser's job.
- With a full ring, an old sector can be recycled during a long download:
  the per-sector identity checks and final manifest re-scan reject that
  case. Retry after a conflict.
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

## Windows ARM build — user evidence (2026-11-01)

The user built the experimental web-download branch on Windows 11 with the
previously configured `build-ncm` and reported:

```text
[141/141] Linking CXX executable environment_monitor.elf
-- Logger partition guard PASS: firmware 115176 bytes < 4 MiB
[PASS] Firmware ELF: D:\my-github\rp2350-environment-monitor\build-ncm\environment_monitor.elf
```

**PASS: Windows GCC 15.2 / Pico SDK 2.3.1 cross-compile, linking,
and 4 MiB image boundary guard.** Binary Logger only image was 105192
bytes in the previous confirmed build; the 9984-byte increase is
consistent with the Web Export module. The transcript does not print
the individual CMake feature flags; if needed, verify
`ENABLE_BINARY_WEB_EXPORT:BOOL=ON` and
`ENABLE_BINARY_FLASH_LOGGER:BOOL=ON` in
`build-ncm/CMakeCache.txt`.

**No RP2350 Flash was performed in this test.** Do not flash the
Web Export-enabled image while the public Cloudflare Tunnel remains
unauthenticated.


### CMake flag verification — 2026-11-01

The user inspected `build-ncm/CMakeCache.txt` after the successful
141/141 Windows cross-build:

```text
ENABLE_BINARY_FLASH_LOGGER:BOOL=ON
ENABLE_BINARY_WEB_EXPORT:BOOL=ON
ENABLE_USB_NCM_WEB_PHASE1:BOOL=ON
```

All three required options are **confirmed ON**. Firmware link,
115176-byte image guard, and enabled feature configuration: **PASS**.
This is still a **build-only** validation. Cloudflare Access remains a
deployment prerequisite; RP2350 HTTP export has not yet been flashed
or tested on-device.


## Access configuration saved — 2026-11-01

Cloudflare Access API confirms application `RP2350 Sensor Log Download` is saved
with two exact destinations `sensor.cafefeed.idv.tw/logs.html` and
`sensor.cafefeed.idv.tw/api/v1/log/*`, one owner-Email Allow policy, and
24-hour session. Edge protection **must still be checked using unauthenticated
HTTP GET requests** before deploying firmware with `ENABLE_BINARY_WEB_EXPORT=ON`.
Verify legacy `/` and `/api/v1/sensors.json` stay reachable without login.



### Actual public ingress HTTP Access smoke test — 2026-11-01

User ran no-browser-cookie `curl.exe` against five public HTTPS URLs and reported:

```text
200  https://sensor.cafefeed.idv.tw/
200  https://sensor.cafefeed.idv.tw/api/v1/sensors.json
302  https://sensor.cafefeed.idv.tw/logs.html
302  https://sensor.cafefeed.idv.tw/api/v1/log/status
302  https://sensor.cafefeed.idv.tw/api/v1/log/list?start=0
```

**PASS:** Public Dashboard and live JSON remain reachable anonymously (HTTP 200); protected Log page, status, list redirect without an Access session (HTTP 302). This is expected for Access and supports correct path selectivity.

**Final security preflight outstanding:** Confirm `Location` header for the 302 points to Cloudflare Access login (not an unrelated origin redirect), and test **direct binary sector** endpoint `/api/v1/log/sector?slot=0&gen=1&crc=0` anonymously also returns Access redirect/denial; no redirect-following or browser cookies. Then back up existing on-device 12 MiB log partition before enabling Web Export, flash the previously built/verified ELF, check anonymous protection remains in place, and perform authenticated browser + Parser tests. Logged data survives firmware update only if actually observed; not yet qualified.



### Protected public ingress and first Web Export ELF flashing — 2026-11-01

User tested unauthenticated public routes directly with `curl.exe`, retaining HTTP response headers:

- `GET /logs.html`: **HTTP 302**, `Location` points to `https://autumn-base-d285.cloudflareaccess.com/cdn-cgi/access/login/sensor.cafefeed.idv.tw`.
- `GET /api/v1/log/sector?slot=0&gen=1&crc=0`: **HTTP 302** with the same Cloudflare Access login destination and protected path encoded in the redirect.
- Earlier anonymous checks: `/` **200**, `/api/v1/sensors.json` **200**, `/api/v1/log/status` **302**, `/api/v1/log/list?start=0` **302**.

**PASS: observed Cloudflare Access authentication redirect at public log page, status/list and actual sector API.** This validates anonymous gating for tested routes, not authorized login or every possible URL variant.

**Pre-update backup:** User ran `picotool save -r 0x10400000 0x11000000 envlog-pre-web.bin -t bin --vid 51966 --pid 16547 -f` **twice**, each 12,582,912 bytes and both before firmware flashing; second command overwrote the file made by the first. Retain this file and parse it to establish a historical baseline before claiming preservation.

**Experimental Web Export firmware successfully flashed:** `./scripts/flash.ps1 -BuildDir build-ncm -Ncm`, USB vendor reboot to PICOBOOT, load 100%, **Verify 100% OK**, application reboot requested. This is an ELF built with all three ON flags (previously verified CMakeCache), 115176 B image, 4 MiB guard PASS.

**Remaining target gates:** after reboot check `http://192.168.7.1/api/v1/log/status`, existing live JSON, authorized `https://sensor.cafefeed.idv.tw/logs.html` login/download, 12 MiB .bin / Python record+CRC validation, and comparison against `envlog-pre-web.bin` to verify FW-update preservation. Do not yet mark on-target Web Download PASS.

## Post-flash local HTTP smoke — PASS (2026-11-01)

After initially seeing a Cloudflare Access login page followed by **HTTP 502
Host Error** at the authenticated public URL, the user checked Windows
USB NCM and queried the RP2350 directly. Actual results:

```text
NCM adapter RP2350 Sensor Experiment USB Network: Up, 12 Mbps
Windows IP: 192.168.7.2/24

GET /                     HTTP 200, total 26.27 ms
GET /api/v1/sensors.json  HTTP 200, total 5.46 ms
GET /api/v1/log/status    HTTP 200, total 101.57 ms
GET /logs.html            HTTP 200, total 16.52 ms

Flash export metadata:
validSectors=350, unknownHeaders=0,
sectorCount=3072, sectorBytes=4096, partitionBytes=12582912
```

Sensor JSON includes all six current sensor groups and SGP40 VOC Index
ready. Cloudflare API shows Tunnel `cafefeed.idv.tw` is **healthy**,
with one Windows connector and four active Cloudflare connections, and
the origin is still `http://192.168.7.1:80`.

These results qualify **local** USB NCM, Dashboard, live JSON and logger
status HTTP. They do **not** establish that authenticated public
`/logs.html` is usable or that binary `/sector` transfer works.
The public HTTP 502 root cause remains open: retest public root versus
the authenticated log path at the same time, and inspect cloudflared
origin connection logs if 502 persists. Do not reflash again solely
because of that 502.

## Cloudflare Access long-header HTTP 502 — targeted fix (2026-11-01)

**Reproduced on RP2350 Web Export firmware**, with `cloudflared` Tunnel healthy
and the same origin hostname serving HTTP 200 on both the local USB NCM path
and public unauthenticated Dashboard. Authenticated `/logs.html` fails with
Cloudflare HTTP 502 (Host Error).

User's **fake** `Cookie: CF_Authorization=...` and
`Cf-Access-Jwt-Assertion: ...` headers, each of the indicated size,
sent **directly to** `192.168.7.1` (no actual secrets):

| Fake header size each | Local origin |
| --- | --- |
| 64 B | HTTP 200 |
| 512 B | HTTP 200 |
| 1500 B | curl (52) empty reply, HTTP 000 |
| 3000 B | curl (52) empty reply, HTTP 000 |

The pinned Pico SDK's lwIP HTTPD uses the upstream
`LWIP_HTTPD_MAX_REQ_LENGTH` and `LWIP_HTTPD_REQ_BUFSIZE` defaults of
**1023 B**, with global `httpd_req_buf` for chained TCP pbufs. The
observed threshold is consistent with its request-header accumulation/
parsing limit. The real Access JWT length was not measured and must
not be copied into bug reports.

**Fix in `src/usb_composite/lwipopts.h` on this branch:**
only when `PROJECT_ENABLE_BINARY_WEB_EXPORT=1`:

```c
#define LWIP_HTTPD_SUPPORT_REQUESTLIST 1
#define LWIP_HTTPD_REQ_QUEUELEN 8
#define LWIP_HTTPD_REQ_BUFSIZE 8192
#define LWIP_HTTPD_MAX_REQ_LENGTH 8192
#define PBUF_POOL_SIZE 8
```

Default NCM build retains `PBUF_POOL_SIZE=4` and upstream
`MAX_REQ_LENGTH` defaults. This adds approximately 7 KiB of shared
HTTPD parser buffer plus four 1600-byte pbufs (with allocation
overhead) in the enabled build; assess actual SRAM use on target.
`MEM_SIZE` and all Sensor/Logger storage logic are unchanged.

**Host CI** verifies the enabled and disabled configurations, existing
Dashboard compilation, binary codec and parser, native C HTTP API
tests, and simulated browser download. These tests do not replace
actual RP2350 network validation.

**Windows validation sequence (do not flash before build PASS):**

```powershell
git switch feature/binary-log-web-download
git pull --ff-only origin feature/binary-log-web-download
.\scripts\build.ps1 -BuildDir build-ncm
# Confirm the image guard PASS and inspect RAM use if available.
# Only after build PASS, using existing USB Vendor Reset updater:
.\scripts\flash.ps1 -BuildDir build-ncm -Ncm
```

**Target regression:** repeat the same 64/512/1500/3000-byte **fake
headers** against `http://192.168.7.1/logs.html`. All four should
return HTTP 200; especially confirm 1500/3000 B no longer drop.
Retest unauthenticated `/logs.html` → Cloudflare Access 302 and
public live `/` and `/api/v1/sensors.json` → 200. Then sign in
and open `https://sensor.cafefeed.idv.tw/logs.html` and download the
12 MiB binary; parse with `tools/envlog_parse.py`, check CRC/samples
and compare with pre-update `envlog-pre-web.bin`. If any step fails,
stop before repeating firmware updates.



### Query-free manifest API verified on target — 2026-11-01

After pulling the browser/lwIP path-segment fix and building/flashing `feature/binary-log-web-download` with the existing Windows USB Vendor Reset workflow, the user's transcript shows:

- `environment_monitor.elf` Build **PASS**.
- `./scripts/flash.ps1 -BuildDir build-ncm -Ncm`: Flash load 100%, Verify 100% OK, reboot requested, no manual BOOTSEL or MSC.
- Local real RP2350 `curl --noproxy '*' http://192.168.7.1/api/v1/log/list/0`: **valid JSON HTTP body** with schema `rp2350.envlog.list.v1`, `start=0`, exactly 64 entries for physical slots **0..63**; recorded `generation` **1..64** and individual Header CRC32 values. This was the previously broken Query String path, now **target-validated PASS**.

The 64 listed headers are **not** proof of HTTP binary payload transfer or of all page CRCs. Next recommended bounded test: `/api/v1/log/sector/0/1/542648003` should produce a 4096-byte binary file. Then retry authenticated browser 12 MiB export and parse with existing `tools/envlog_parse.py`; compare pre-update historical records. Do not declare end-to-end download PASS before that evidence.



### 4 KiB Binary Sector API on-target PASS — 2026-11-01

Following successful Windows cross-build, verified USB Vendor Flash, and Query-free Manifest HTTP JSON PASS, user executed the **actual target** binary response test:

```text
curl --noproxy '*' -sS --max-time 20 -o envlog-sector0.bin \
  http://192.168.7.1/api/v1/log/sector/0/1/542648003
HTTP=200 Bytes=4096
(Get-Item .\\envlog-sector0.bin).Length = 4096
```

**PASS:** HTTP 200 and exact 4096-byte sector binary transport for physical slot 0, generation 1, manifest-provided sector header CRC32 542648003. This verifies that the path-segment sector API returns a complete transfer on RP2350 over USB NCM. **Not yet tested:** independent CRC integrity for the downloaded 4 KiB bytes; complete authenticated browser 12 MiB image reconstruction; per-page CRC, record count, historical data retention compared with pre-update dump. No further Firmware changes or reflashing indicated by this result.

**Next step:** retry authenticated `https://sensor.cafefeed.idv.tw/logs.html` → Download Binary Log, confirm file length 12,582,912 bytes, then parse the downloaded file with `tools/envlog_parse.py` and check for `0 warnings`, comparing against `envlog-pre-web.bin` without assuming sample count is unchanged while logging continues.

## Validation gates

- [x] Native C export server test for bounded address and generation,
  request lock, HTTP status/list/sector, malformed query
- [x] Browser JS syntax test in CI
- [x] Full existing Dashboard HTTPD native syntax check with export ON
- [x] Browser automation-style Node contract test: synthetic 12 MiB
      download and second-pass generation-race rejection
- [x] ARM Windows build, 141/141 steps, 115176 B image and 4 MiB guard PASS; all three feature flags independently verified ON in CMakeCache
- [x] Existing live JSON Dashboard smoke over local NCM (HTTP 200; six sensor groups reported)
- [ ] Actual localhost USB CDC-NCM browser download (local /logs.html and /status are HTTP 200; binary sector transfer pending)
- [x] Cloudflare Access app/owner policy saved; public root and live JSON HTTP 200, anonymous Log page/status/list HTTP 302
- [x] Check Access redirect Location and direct /api/v1/log/sector anonymous denial (both HTTP 302 to Cloudflare Access)
- [ ] Authenticated download after firmware flashing and parser CRC/record verification
- [ ] On-device concurrent logging + downloadable data CRC tests
- [ ] Full 12 MiB export latency and 1 Hz sampling effect

**Never assume Cloudflare Access is enabled based on a successful firmware build.**
