#!/usr/bin/env python3
"""Read-only RFC5905 SNTP/NTP probe for a future RP2350 UTC anchor.

Runs on the Windows host, NOT the RP2350 (which has no Internet gateway).
This tool never writes time into the MCU, modifies the host clock, updates
firmware, or creates an untrusted public HTTP time-setting endpoint.
Requires Python 3.9+; UDP/123 must be reachable from the Windows host.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
import socket
import struct
import time

NTP_UNIX_DELTA = 2208988800
NTP_FORMAT = "!II"
PACKET_SIZE = 48


class NtpError(RuntimeError):
    pass


def _to_ntp(t: float) -> bytes:
    seconds = int(t) + NTP_UNIX_DELTA
    fraction = int((t - int(t)) * (2**32))
    if not 0 <= seconds < 2**32:
        raise NtpError("NTP-era 0 overflow, do not generate ambiguous timestamp")
    return struct.pack(NTP_FORMAT, seconds, fraction)


def _from_ntp(raw: bytes) -> float:
    if len(raw) != 8:
        raise NtpError("NTP timestamp must be exactly 8 bytes")
    seconds, fraction = struct.unpack(NTP_FORMAT, raw)
    return (seconds - NTP_UNIX_DELTA) + fraction / (2**32)


def parse_response(data: bytes, sent: bytes, t1: float, t4: float) -> dict:
    """Validate a unicast server reply and estimate corrected UTC at t4.

    RFC 5905:
      offset = ((T2-T1)+(T3-T4))/2
      network_delay = (T4-T1)-(T3-T2)
    The accuracy estimate is a bound proxy, not cryptographic trust.
    """
    if len(data) < PACKET_SIZE or len(sent) != PACKET_SIZE:
        raise NtpError("Incomplete NTP request/reply")
    li, version, mode = data[0] >> 6, (data[0] >> 3) & 7, data[0] & 7
    stratum = data[1]
    if li == 3:
        raise NtpError("Server reports unsynchronized clock (LI=3)")
    if version not in (3, 4) or mode != 4:
        raise NtpError("Unexpected NTP version or packet mode")
    if not 1 <= stratum <= 15:
        raise NtpError("Unsynchronized / kiss-of-death server (stratum)")
    if data[24:32] != sent[40:48]:
        raise NtpError("NTP response originate timestamp mismatch")
    if data[32:40] == b"\0" * 8 or data[40:48] == b"\0" * 8:
        raise NtpError("NTP transmit/receive timestamps are missing")
    t2, t3 = _from_ntp(data[32:40]), _from_ntp(data[40:48])
    offset = ((t2 - t1) + (t3 - t4)) / 2.0
    delay = (t4 - t1) - (t3 - t2)
    if delay < -0.002 or delay > 3.0:
        raise NtpError("Implausible NTP network round-trip delay")
    if abs(offset) > 3600:
        raise NtpError("Host clock differs by >1 hour; investigate before anchoring")
    root_dispersion = int.from_bytes(data[8:12], "big") / 65536.0
    if root_dispersion > 10.0:
        raise NtpError("NTP root dispersion too large")
    uncertainty_ms = (max(0, delay)/2 + root_dispersion) * 1000
    utc_t4 = t4 + offset
    return {
        "schema": "rp2350.ntp.host_probe.v1",
        "utc": dt.datetime.fromtimestamp(utc_t4, dt.timezone.utc).isoformat(timespec="milliseconds"),
        "utc_unix_ms": round(utc_t4 * 1000),
        "offset_ms": round(offset * 1000, 3),
        "round_trip_ms": round(max(delay, 0) * 1000, 3),
        "root_dispersion_ms": round(root_dispersion * 1000, 3),
        "estimated_uncertainty_ms": round(uncertainty_ms, 3),
        "stratum": stratum,
        "leap_indicator": li,
    }


def query(server: str, timeout: float = 3.0) -> dict:
    hosts = socket.getaddrinfo(server, 123, socket.AF_INET, socket.SOCK_DGRAM)
    if not hosts:
        raise NtpError("NTP DNS resolution returned no IPv4 endpoint")
    last_error = None
    for af, socktype, proto, _canon, addr in hosts:
        try:
            with socket.socket(af, socktype, proto) as sock:
                sock.settimeout(timeout)
                req = bytearray(PACKET_SIZE)
                req[0] = (4 << 3) | 3   # LI=0, NTPv4, client mode=3
                t1 = time.time()
                req[40:48] = _to_ntp(t1)
                sock.sendto(req, addr)
                packet, peer = sock.recvfrom(512)
                t4 = time.time()
                if peer[0] != addr[0] or peer[1] != 123:
                    raise NtpError("Reply source address or port mismatch")
                result = parse_response(packet,bytes(req),t1,t4)
                result["source"] = server
                result["server_ip"] = peer[0]
                return result
        except (OSError, NtpError) as exc:
            last_error = str(exc)
    raise NtpError(f"No trusted NTP response: {last_error}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--server", default="time.cloudflare.com",
                    help="NTP server hostname (default: time.cloudflare.com)")
    ap.add_argument("--timeout", type=float, default=3)
    ap.add_argument("--max-uncertainty-ms", type=float, default=1000,
                    help="reject excessively uncertain time estimates")
    args = ap.parse_args()
    try:
        if args.timeout <= 0 or args.max_uncertainty_ms <= 0:
            raise NtpError("Timeout and uncertainty threshold must be positive")
        result = query(args.server,args.timeout)
        if result["estimated_uncertainty_ms"] > args.max_uncertainty_ms:
            raise NtpError("NTP estimate exceeds uncertainty threshold")
        result["status"] = "probe_only_not_applied_to_rp2350"
        print(json.dumps(result,indent=2))
        return 0
    except (NtpError, OSError, ValueError) as exc:
        print(json.dumps({"status":"unavailable","reason":str(exc)}))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
