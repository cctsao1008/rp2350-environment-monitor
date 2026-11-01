#!/usr/bin/env python3
"""Read-only daily backup of the RP2350 12 MiB ELOG partition over local USB NCM.

Uses only the existing bounded GET endpoints, never PICOBOOT, writes, or resets.
Requires Python >= 3.9 and the adjacent repository's envlog_parse.py.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

import envlog_parse as elog

BASE_URL = "http://192.168.7.1"
MANIFEST_STRIDE = 64


class BackupError(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def validate_base_url(value):
    parsed = urllib.parse.urlsplit(value)
    if (parsed.scheme != "http" or parsed.hostname not in
            ("192.168.7.1", "127.0.0.1") or parsed.username or
            parsed.password or parsed.query or parsed.fragment or
            parsed.path not in ("", "/")):
        raise BackupError("URL must be local USB NCM http://192.168.7.1 "
                          "(127.0.0.1 allowed for tests)")
    return value.rstrip("/")


class Client:
    def __init__(self, base_url=BASE_URL, timeout=10):
        self.base_url = validate_base_url(base_url)
        self.timeout = timeout
        # Ignore OS/enterprise HTTP proxies; don't follow redirects.
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect())

    def get(self, path, limit):
        url = self.base_url + path
        req = urllib.request.Request(url, headers={
            "Accept": "application/json, application/octet-stream",
            "Cache-Control": "no-store",
        }, method="GET")
        try:
            with self.opener.open(req, timeout=self.timeout) as r:
                if r.status != 200:
                    raise BackupError(f"HTTP {r.status}: {path}")
                data = r.read(limit + 1)
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise BackupError(f"HTTP GET {path}: {exc}") from exc
        if len(data) > limit:
            raise BackupError(f"Oversized response: {path}")
        return data

    def json(self, path, limit=4096):
        try:
            return json.loads(self.get(path, limit))
        except (UnicodeError, ValueError) as exc:
            raise BackupError(f"Invalid JSON: {path}") from exc


def status(client):
    obj = client.json("/api/v1/log/status")
    expected = {"schema": "rp2350.envlog.export.v1", "version": 1,
                "sectorBytes": elog.SECTOR,
                "sectorCount": elog.PARTITION_SIZE // elog.SECTOR,
                "partitionBytes": elog.PARTITION_SIZE,
                "manifestPage": MANIFEST_STRIDE, "unknownHeaders": 0}
    if not isinstance(obj, dict) or any(obj.get(k) != v for k, v in expected.items()):
        raise BackupError(f"Incompatible or unknown ELOG partition: {obj}")
    if (not isinstance(obj.get("validSectors"), int) or
            not 0 <= obj["validSectors"] <= expected["sectorCount"]):
        raise BackupError("Invalid validSectors count")
    return obj


def manifest(client):
    identities = {}
    count = elog.PARTITION_SIZE // elog.SECTOR
    for start in range(0, count, MANIFEST_STRIDE):
        obj = client.json(f"/api/v1/log/list/{start}")
        if (not isinstance(obj, dict) or
                obj.get("schema") != "rp2350.envlog.list.v1" or
                obj.get("start") != start or
                not isinstance(obj.get("entries"), list) or
                len(obj["entries"]) > MANIFEST_STRIDE):
            raise BackupError(f"Invalid manifest page {start}")
        for entry in obj["entries"]:
            if (not isinstance(entry, list) or len(entry) != 3 or
                    any(type(n) is not int or not 0 <= n <= 0xffffffff
                        for n in entry)):
                raise BackupError(f"Malformed identity on page {start}")
            slot, generation, crc = entry
            if not start <= slot < min(start + MANIFEST_STRIDE, count):
                raise BackupError(f"Out-of-page sector slot {slot}")
            if slot in identities:
                raise BackupError(f"Duplicate sector slot {slot}")
            identities[slot] = (generation, crc)
    return identities


def capture(client):
    """Return verified image and metadata; never modifies the RP2350."""
    before = status(client)
    first = manifest(client)
    image = bytearray(b"\xff" * elog.PARTITION_SIZE)
    for slot, (generation, crc) in first.items():
        path = f"/api/v1/log/sector/{slot}/{generation}/{crc}"
        data = client.get(path, elog.SECTOR)
        if len(data) != elog.SECTOR:
            raise BackupError(f"Short sector {slot}: {len(data)} bytes")
        meta = elog.sector_header(data[:elog.HEADER])
        if (meta is None or meta["generation"] != generation or
                struct.unpack_from("<I", data, 252)[0] != crc):
            raise BackupError(f"Sector {slot} identity/CRC mismatch")
        pos = slot * elog.SECTOR
        image[pos:pos + elog.SECTOR] = data

    # The logger may append new sectors meanwhile. An existing identity must
    # not change. This is incremental capture, not an atomic flash snapshot.
    second = manifest(client)
    if any(second.get(slot) != identity for slot, identity in first.items()):
        raise BackupError("Sector recycled during capture; retry from scratch")
    after = status(client)
    if len(second) < len(first) or after["unknownHeaders"]:
        raise BackupError("Logger changed unexpectedly during capture")

    info = elog.inspect_headers(image)
    if (info["valid_headers"] != len(first) or
            info["invalid_or_unknown_sectors"] != 0):
        raise BackupError(f"Unrecognized sectors in image: {info}")
    rows, problems = elog.scan(image)
    if problems:
        raise BackupError(f"ELOG page/sector validation failed: {problems[:3]}")
    summary = {
        "schema": "rp2350.envlog.hostbackup.v1",
        "source": client.base_url,
        "capture_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "size_bytes": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "valid_sectors": info["valid_headers"],
        "samples": len(rows),
        "sessions": len({r["session_id"] for r in rows}),
        "utc_samples": sum(bool(r["timestamp_utc"]) for r in rows),
        "parser_warnings": 0,
        "start_valid_sectors": before["validSectors"],
        "end_valid_sectors": after["validSectors"],
        "capture_mode": "incremental_non_atomic",
    }
    return bytes(image), summary


def save_verified(image, summary, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H-%M-%S-%fZ")
    prefix = f"environment-log-backup-{stamp}"
    target = out_dir / (prefix + ".bin")
    sidecar = out_dir / (prefix + ".json")
    # Partial file lives on the destination filesystem. Never truncate a prior
    # backup, and publish binary only after the whole verified image is written.
    fd, tmp = tempfile.mkstemp(prefix=".envlog-", suffix=".partial", dir=out_dir)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(image)
            f.flush()
            os.fsync(f.fileno())
        if target.exists() or sidecar.exists():
            raise BackupError("Backup destination already exists")
        os.replace(tmp, target)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    try:
        with sidecar.open("x", encoding="utf-8") as f:
            json.dump(summary, f, indent=2, sort_keys=True)
            f.write("\n")
    except OSError as exc:
        raise BackupError(f"Binary saved at {target}; metadata write failed: {exc}") from exc
    return target, sidecar


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--outdir", type=Path, default=Path.home() / "RP2350-Log-Backups")
    p.add_argument("--base-url", default=BASE_URL, help="Local USB NCM origin (no public tunnel)")
    p.add_argument("--timeout", type=float, default=10)
    p.add_argument("--attempts", type=int, default=3)
    args = p.parse_args(argv)
    if args.timeout <= 0 or not 1 <= args.attempts <= 10:
        p.error("--timeout must be positive; --attempts must be 1..10")
    try:
        client = Client(args.base_url, args.timeout)
        for attempt in range(1, args.attempts + 1):
            try:
                image, summary = capture(client)
                target, meta = save_verified(image, summary, args.outdir)
                print(f"PASS: {summary['samples']} samples, "
                      f"{summary['utc_samples']} UTC, "
                      f"{summary['valid_sectors']} sectors, 0 warnings")
                print(f"BIN: {target}\nJSON: {meta}\nSHA256: {summary['sha256']}")
                return 0
            except BackupError as exc:
                print(f"Attempt {attempt}/{args.attempts}: {exc}", file=sys.stderr)
                if attempt == args.attempts:
                    raise
                time.sleep(1)
    except (BackupError, OSError) as exc:
        print(f"FAIL: no new verified backup published: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
