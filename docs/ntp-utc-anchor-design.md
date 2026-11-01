# P0: NTP-based UTC Anchors — host discovery and design

**Status: host-side NTP probe only. No RP2350 firmware or Flash changes.**
Parent source: `feature/binary-log-web-download`.
Issue: https://github.com/cctsao1008/rp2350-environment-monitor/issues/4

## Architecture constraint verified in existing firmware

RP2350 uses USB CDC-NCM with IP `192.168.7.1/24`.
The Windows USB host is `192.168.7.2`, and the MCU's default
gateway is `0.0.0.0`. Public Cloudflare Tunnel HTTP forwarding
does not supply an outbound IPv4 route for direct UDP NTP.
**Do not assume that configuring `pool.ntp.org` in MCU code is enough.**
Changing Windows Internet Connection Sharing or global routing has
security and operation implications and is not part of this design.

## Stage 1 — verify NTP from Windows Host (implemented)

Python 3.9+ on the Windows host:

```powershell
py .\tools\ntp_time_probe.py --server time.cloudflare.com
```

This SNTP/NTPv4 client sends a 48-byte RFC5905 unicast request over
UDP/123 and validates that the server response is synchronized
(leap indicator !=3, stratum 1..15), originates from the queried endpoint
and echoes the exact request transmit timestamp. It rejects large
delay, huge clock offsets and excessive reported root dispersion.
It reports:
- Estimated corrected UTC and Unix milliseconds
- NTP clock offset, round-trip delay, server root dispersion
- Estimated uncertainty (half RTT + root dispersion)
- Server identity, leap indicator, stratum and a `probe_only` status

**This is NOT yet a reliable persistent UTC anchor on RP2350.**
This unauthenticated NTP protocol estimates consistency/uncertainty
but does not cryptographically authenticate the NTP source; for
stronger adversarial trust, use NTS or a trusted LAN time service.
This stage does not set Windows system time, open HTTP routes,
send packets to the RP2350, or update the ELOG format.

References:
- RFC5905: https://www.rfc-editor.org/rfc/rfc5905
- Microsoft Windows Time Service: https://learn.microsoft.com/en-us/windows-server/networking/windows-time-service/windows-time-service-tools-and-settings

## Stage 2 — scoped delivery and persistent Flash metadata (NOT implemented)

1. Identify Windows NTP service and UDP/123 policy. Direct host
   `w32tm /query /source` and `w32tm /query /status` do not guarantee
   the chosen server is Internet NTP or authenticated, but can help
   inspect the Windows time source. Probe result is an independent check.
2. Deliver UTC anchor from Windows through **physically local USB**
   channel (CDC console or an allowlisted UDP port). Never expose an
   unauthenticated `GET /api/v1/time/set` through the Tunnel.
3. Capture boot-relative monotonic receive timestamp on MCU. Keep every
   1 Hz sample in monotonic time and map only if the relevant Session
   has a verified anchor. Avoid sample-time jumps during resync.
4. Store UTC and monotonic reference as **append-only, CRC-protected
   anchor metadata**. The existing ELOG v1 sector header has
   `utc_valid` at byte 39 and unused 40..251, with CRC32 at 252.
   Newly opened sectors can carry metadata before their 256-byte
   header page is programmed; **existing headers cannot be rewritten
   in place** (NOR 0->1 requires erase, breaking validated history).
   Evaluate a separate time-anchor journal if first-session anchoring
   before the next sector is required.
5. Extend Host Parser + protected History to show exact UTC for only
   sessions with anchors. Old sessions remain `time_unknown` instead
   of fabricating calendar dates. Support time-anchor uncertainty,
   source age, host jitter and monotonic wrap.
6. Validate NTP unavailable, USB disconnected, device reboot,
   duplicate anchors, drift, clock step, and power-loss before
   Flash changes are released.

## Stage 1 acceptance

- [x] Read-only host NTPv4 probe implemented without dependencies
- [x] Server mode/leap/stratum/originate/dispersion/delay checks
- [x] Offline RFC5905 T1/T2/T3/T4 response tests
- [ ] Windows host physical UDP/123 reachability and source quality
- [ ] Time anchor delivery protocol and MCU persistence
- [ ] RP2350 real-time UTC mapping verification

No commitment to UTC accuracy before actual source tests.
