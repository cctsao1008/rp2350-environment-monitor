# Native RP2350 SNTP / UTC Anchor — main branch

**Status:** Source code integrated directly into `main`, host-only
time bridge discarded. **Windows ARM build and live network/RTC
verification are still required before Flash.** The existing 12 MiB
Flash logger contents must remain intact.

## Architecture

```text
  time.cloudflare.com (Internet NTP, UDP/123)
             ^
             | outbound UDP/123, reply packets
             |
  Windows IPv4 routing/NAT (no NTP application or time proxy)
             ^
             | USB CDC-NCM network: 192.168.7.0/24
             |
  RP2350 at 192.168.7.1, gateway 192.168.7.2
    lwIP DNS resolver: 1.1.1.1 via routed USB NIC
    lwIP pico_lwip_sntp → DNS → UDP/123 → NTP server
             |
       monotonic + UTC mapping (SRAM)
             |
    ELOG sector immutable header: CRC-protected UTC anchor
             |
   Host Python Parser and protected History UI
```

**The NTP client actually runs on RP2350**, not Windows. The Windows
host is an IPv4 forwarding/NAT hop only; it does NOT provide time.
Cloudflare Tunnel's inbound HTTP ingress does not substitute for
a public egress route, so RP2350 **cannot synchronize** until the
Windows NAT/forwarding path and UDP/123 access are actually functional.

The NCM DHCP server continues advertising `router=0.0.0.0` to Windows
to avoid putting an incorrect host default route through the MCU.
The RP2350's own lwIP default gateway becomes `192.168.7.2` when
`ENABLE_SNTP_TIME=ON`.

## Firmware integration

- Pico SDK `pico_lwip_sntp`, SNTPv4 unicast via UDP.
- `SNTP_SERVER_DNS=1`, `SNTP_CHECK_RESPONSE=2`: validates response
  source address/port and echoed originate timestamp. Default server
  `time.cloudflare.com`, DNS resolver `1.1.1.1`.
- A fresh request is sent at startup and repeated on the lwIP SNTP
  1-hour polling interval; exponential retry when unavailable.
- `SNTP_SET_SYSTEM_TIME_US` stores the server's UTC transmit time at
  RP2350 receive monotonic time. No modification of the sensor's
  boot-relative time or Windows system clock.
- Failed/absent NTP leaves all UTC fields explicitly unsynchronized.
  Old anchors are considered stale after 6 hours.
- Public live `/api/v1/sensors.json` adds read-only `ntp`:
  `synchronized`, server/source, `utc_unix_s`,
  `utc_microseconds`, `age_ms`, `updates`; no write API.

**Important accuracy qualification:** Since the board has no trusted
RTC, the first reply cannot be corrected using RFC5905 four timestamps.
Using server transmit time at receipt introduces an unknown one-way
network delay. lwIP's SNTP code also does not implement all advanced
leap/stratum validation. NTP here is unauthenticated (no NTS).
Treat this as approximate UTC for environmental analytics, **not**
a cryptographic source of true time nor a high-precision synchronized
clock.

## Non-destructive Flash format extension

Existing ELOG v1 Sector Header (256 bytes, CRC32 over bytes 0..251)
has reserved bytes and retains the same Record 24 B / Page 256 B /
Sector 4096 B geometry.

When the logger opens a **new** Sector and there is a fresh SNTP
anchor, it writes:
- `header[39] = 1`: UTC present
- `header[40..47]`: Unix UTC microseconds, little-endian uint64
- `header[48..55]`: RP2350 boot monotonic microseconds, LE uint64
- `header[56] = 1`: anchor layout version
- `header[252..255]`: recomputed metadata CRC32

Older sectors remain immutable; no erase/rewrite of historical
metadata solely to apply NTP. UTC only appears in a new sector after
sync is obtained; until then, live JSON can show synced UTC while
earlier sector headers are still unanchored. A new sector normally
starts after 150 one-second samples, barring resets/gaps.

Host parser chooses the latest valid UTC anchor from **the same
`session_id`**, and reports `timestamp_utc` and `utc_quality`.
Older sessions without anchors continue displaying boot-relative time.
For records earlier than the first anchor in the same session,
the parser labels `sntp_back_extrapolated`; this is a retrospective
estimate, not an independently time-synchronized measurement.
The protected History page uses UTC for anchored sessions and
boot-relative time for unanchored sessions.

## Windows prerequisites (NOT performed by GitHub edits)

**Do not blindly enable Windows ICS**: ICS often changes NIC subnet
to 192.168.137.0/24, breaking the existing `192.168.7.1` Tunnel.
Do not remove an existing WinNAT; Windows commonly allows only
one NAT prefix for multiple virtual networks, and WSL/Hyper-V/Docker
may own it.

Inspect in an elevated PowerShell:

```powershell
Get-NetNat | Format-Table Name,InternalIPInterfaceAddressPrefix
Get-NetAdapter | Where-Object InterfaceDescription -Match 'NCM'
Get-NetIPAddress -AddressFamily IPv4 | Where-Object IPAddress -Like '192.168.7.*'
Get-NetIPInterface -AddressFamily IPv4 |
    Select-Object InterfaceAlias,Forwarding,ConnectionState
```

**If no existing NAT and company/network policy permits it**,
Windows can potentially provide packet routing through:

```powershell
# Example ONLY: requires administrator privileges and security review.
New-NetNat -Name "RP2350-NCM-NAT" -InternalIPInterfaceAddressPrefix "192.168.7.0/24"
# Also configure forwarding on the actual USB NCM AND Internet uplink
# interface; review adapter aliases before any Set-NetIPInterface changes.
```

WinNAT `New-NetNat` alone does not allocate addresses or necessarily
enable interface forwarding. The NCM device and Windows already
own `192.168.7.1` and `192.168.7.2`; preserve them.
Confirm DNS UDP/53 and NTP UDP/123 reach Internet. Where firewall/
enterprise routing forbids this, RP2350 stays `synchronized=false`
and 1 Hz logging continues normally. No hidden Windows script should
act as an NTP time bridge.

References:
- [Pico SDK lwIP SNTP](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)
- [lwIP SNTP API](https://www.nongnu.org/lwip/2_1_x/group__sntp.html)
- [Microsoft WinNAT](https://learn.microsoft.com/virtualization/hyper-v-on-windows/user-guide/setup-nat-network)

## Validation

- [x] Existing `main` updated by safe fast-forward from proven Web
  Export + P3 History code; no force push.
- [x] RP2350 native DNS/SNTP C implementation committed to `main`.
- [x] Immutable CRC-covered ELOG anchor writer and parser added.
- [x] Legacy no-anchor header and UTC mapping host test vectors added.
- [ ] GitHub Actions on `main` verify native C codec, Python and
  Browser tests after the latest commit.
- [x] Windows ARM GCC/Pico SDK cross-compile and ELF 4 MiB guard (140,420-byte image); detailed SRAM usage remains unqualified.
- [x] Windows WinNAT active, NCM forwarding enabled, RP2350 actual DNS/SNTP outbound reply confirmed; Wi-Fi IPv4 Forwarding remained Disabled.
- [x] Real `/api/v1/sensors.json` returned `ntp.synchronized=true`, `source=rp2350-sntp`, `updates=1`, plausible UTC.
- [x] Web-exported 12 MiB ELOG binary included 630 UTC-mapped records in Session 9 (628 `sntp_anchor`, 2 `sntp_back_extrapolated`), 0 Parser warnings.
- [ ] Historical Dashboard Chrome UTC calendar axis target validation.
- [x] 53,980-record historical baseline preserved: new image 59,690 records, +5,710 newly decoded, 0 missing (comparison excludes derived UTC fields), 0 warnings.
- [ ] Explicit reset during ongoing logging with no data loss beyond documented pending-page policy.
- [ ] Clock step, loss-of-network, stale anchor, 1Hz sampling and
  Flash circular wrap reliability.

## Target verification evidence — 2026-11-01

The project owner built and flashed the mainline image using the existing
Vendor Reset + Picotool ELF/verify path. RP2350-originated SNTP succeeded
through Windows USB-NCM WinNAT without enabling Wi-Fi IPv4 forwarding.
Web Binary Export yielded a 12,582,912-byte image: 404 valid ELOG
headers, 59,690 decoded samples, 7 data-bearing sessions, 630 records
with UTC, and 0 warnings. All 53,980 earlier records were retained in
the new snapshot when comparing original fields (excluding derived UTC).
These are actual on-target/host results, not synthetic-only tests.

Remaining separate qualifications: History UI UTC axis in Chrome,
NTP re-sync/long outage, clock step/drift, full NOR ring recycle,
unexpected power failure and long-duration sampling jitter.

The host read-only backup utility is documented at
[Windows Host Backup](host-backup.md). It has passing host CI, but the
actual Windows USB-NCM backup and daily Task Scheduler automation
remain pending physical host validation.
