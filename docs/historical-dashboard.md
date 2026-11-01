# P3: Protected Historical Dashboard (experimental)

**Branch:** `feature/historical-dashboard`, based on the verified
`feature/binary-log-web-download` code. **Draft PR #5**, unmerged.

## User-visible feature

The existing `/logs.html` download page links to
`/api/v1/log/history`.

**Security:** `/api/v1/log/history` is deliberately inside the existing
Cloudflare Access-protected `/api/v1/log/*` namespace. It does not
create a new publicly readable history endpoint. The legacy
`/` and `/api/v1/sensors.json` remain public and unmodified.
Check actual no-cookie public response returns Cloudflare Access 302 or
403 before deployment. Current Microcontroller HTTPD does not
independently verify Access credentials.

When the owner clicks **Load Historical Data**:

1. Browser reads read-only `/api/v1/log/status` and all 48 paged
   `/api/v1/log/list/N` manifests (64 physical slots each).
2. Sort by wrap-aware Flash generation to present sessions in sequence,
   rather than by physical sector position.
3. Fetch **only** valid ELOG 4 KiB sectors using
   `/api/v1/log/sector/SLOT/GENERATION/HEADER_CRC`.
4. Validate header magic, format, partition geometry, generation and
   **CRC32**. Validate each 256-byte page CRC32 excluding the
   4-byte CRC field, 24-byte record ordering, reserved/quality bits,
   page sentinel 0xffff, and monotonic sequence continuity within
   each data-bearing session.
5. Aggregate valid sensor readings in browser memory into five-minute
   buckets *per boot Session*, skipping invalid or warming-up values.
   Do not resample, change or write to NOR Flash.
6. Re-read all manifests and abort if an earlier sector was overwritten
   while loading. Newly created sectors are not a failure; this is
   a consistent **incremental** capture, not a frozen snapshot.
7. Render chart on HTML Canvas (no chart library, JavaScript package,
   CDN, database or additional cloud account). Session select and
   metric selector include temperature, RH, pressure, optical ADC
   counts, SGP40 SRAW and relative VOC Index. Show session summary.

The RP2350 provides the same existing bounded HTTP data, and does
**not** calculate any trend or aggregate. Browser traffic may be
substantial: around one sector request per 4 KiB stored, but no
12 MiB array is allocated on MCU. The browser keeps per-session
5-minute summary buckets, not every 1 Hz sample.

## Timestamp behavior: no fabricated calendar dates

`ELOG v1` stores session-relative monotonic `timestamp_us`. No
verified UTC time anchor is present in earlier logs. The History UI
**must label its X axis as boot-relative time**; even though the binary
download filename has an ISO date, it cannot assign that date to all
sessions. Integration with NTP-derived UTC anchor is tracked separately
in [Issue #4](https://github.com/cctsao1008/rp2350-environment-monitor/issues/4).

## Host validation

- [x] Native C route test: `/api/v1/log/history` reachable; unrelated
  `/history.html` not exposed.
- [x] History embedded JS syntax and synthetic 24-byte/256-byte parser
  unit test in GitHub Actions.
- [x] Browser acceptance simulation: valid 20-record sector, bad CRC,
  and ring-generation replacement; valid case renders chart, bad cases
  abort without silently displaying corrupt history.
- [ ] Windows ARM cross-build / post-link Flash-partition guard.
- [ ] Local NCM page HTTP 200; `/api/v1/log/status` and
  `/api/v1/log/list/0` HTTP 200.
- [ ] Public anonymous `/api/v1/log/history` must redirect to Access
  (302/403), while public live Dashboard remains HTTP 200.
- [ ] Authenticated actual RP2350 load of protected History page,
  record/session counts compared with offline `envlog_parse.py`
  from the same approximate capture.
- [ ] Long-running 1 Hz logging / USB network latency, browser
  memory usage with all 3072 sectors.
- [ ] Full ring recycle race / power-cut behavior on target.

**Do not flash the new image solely because Host CI passes.**
Retain existing 53,980-record verified binaries as a regression baseline.

## P0: NTP-based UTC time anchor

Current `usb_ncm_network.c` specifies default gateway 0.0.0.0,
hence RP2350 cannot directly contact a public UDP/123 NTP server.
Use a Windows-host NTP bridge instead: source time from RFC 5905
NTP (e.g. Windows time service), exchange boot-local monotonic
reference over the physically attached USB-NCM/CDC link, then
store an immutable, CRC-protected per-session anchor on device.
This needs a **separate review before Flash changes**, including
correct uncertainty and recovery behavior. No Time Anchor changes
have been applied as part of P3.
