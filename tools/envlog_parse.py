#!/usr/bin/env python3
"""Offline decoder for RP2350 Environment Binary Logger v1.

Input is a USB Vendor Reset / picotool read of flash 0x10400000..0x11000000.
No HTTP endpoint or automatic network access is used. Requires Python 3.9+.
"""
import argparse
import csv
import struct
import sys
import zlib
from pathlib import Path

SECTOR = 4096
PAGE = 256
HEADER = 256
RECORD = 24
PARTITION_SIZE = 12 * 1024 * 1024
PARTITION_OFFSET = 4 * 1024 * 1024
FLAGS = {
    "bme_ok": 0x01, "tsl_ok": 0x02, "ltr_ok": 0x04, "sgp_ok": 0x08,
    "tsl_saturated": 0x10, "voc_ready": 0x20,
}
FIELDS = [
    "generation", "session_id", "sequence", "timestamp_monotonic_us",
    "bme_valid", "temp_c", "rh_percent", "pressure_pa",
    "tsl_valid", "tsl_saturated", "tsl_ch0", "tsl_ir", "tsl_lux_relative",
    "ltr_valid", "ltr_als", "ltr_uv", "ltr_lux", "ltr_uvi",
    "sgp_valid", "voc_ready", "sgp_sraw", "voc_index", "flags",
]

def u24(buf, at):
    return int.from_bytes(buf[at:at+3], "little")

def crc32(buf):
    return zlib.crc32(buf) & 0xffffffff

def sector_header(page):
    if len(page) != HEADER or page[:4] != b"ELOG":
        return None
    ver, length = struct.unpack_from("<HH", page, 4)
    if ver != 1 or length != HEADER:
        return None
    off, size = struct.unpack_from("<II", page, 16)
    rec_len, pg_len, sec_len, nrec = struct.unpack_from("<HHHH", page, 24)
    if (off, size, rec_len, pg_len, sec_len, nrec) != (
        PARTITION_OFFSET, PARTITION_SIZE, RECORD, PAGE, SECTOR, 10
    ):
        return None
    if struct.unpack_from("<I", page, 252)[0] != crc32(page[:252]):
        return None
    return {
        "generation": struct.unpack_from("<I", page, 8)[0],
        "session_id": struct.unpack_from("<I", page, 12)[0],
        "tsl_df": struct.unpack_from("<H", page, 32)[0],
        "tsl_scale": struct.unpack_from("<H", page, 34)[0] / 1000,
        "tsl_control": page[36],
        "ltr_rate": page[37],
        "ltr_gain": page[38],
        "utc_valid": bool(page[39]),
    }

def page_rows(page, meta):
    """Returns (rows, error). Only committed and CRC-valid pages are accepted."""
    if len(page) != PAGE:
        return [], "short_page"
    if page == b"\xff" * PAGE:
        return [], None
    (base_us, base_seq, stored_crc) = struct.unpack_from("<QII", page, 0)
    if stored_crc != crc32(page[:12] + page[16:]):
        return [], "crc32_mismatch"
    rows = []
    prev_offset = -1
    ended = False
    for i in range(10):
        rec = page[16+i*RECORD:16+(i+1)*RECORD]
        delta = struct.unpack_from("<H", rec)[0]
        if delta == 0xffff:
            if rec != b"\xff" * RECORD:
                return [], "invalid_unused_slot"
            ended = True
            continue
        if ended or (i == 0 and delta != 0) or delta < prev_offset:
            return [], "invalid_record_order"
        prev_offset = delta
        f = rec[23]
        if f & 0xc0:
            return [], "unknown_quality_bits"
        bme = bool(f & FLAGS["bme_ok"])
        tsl = bool(f & FLAGS["tsl_ok"])
        ltr = bool(f & FLAGS["ltr_ok"])
        sgp = bool(f & FLAGS["sgp_ok"])
        t, rh = struct.unpack_from("<hH", rec, 2)
        pressure = u24(rec, 6)
        ch0, ir = struct.unpack_from("<HH", rec, 9)
        als, uv = u24(rec, 13), u24(rec, 16)
        raw_voc, index = struct.unpack_from("<HH", rec, 19)
        saturated = bool(f & FLAGS["tsl_saturated"])
        ready = bool(f & FLAGS["voc_ready"])
        if saturated and not tsl or ready and not sgp:
            return [], "contradictory_flags"
        lux = None
        if tsl and not saturated and ch0 and meta["tsl_df"]:
            cpl = (100.0 * 25.0) / meta["tsl_df"]
            ir_ratio = ir / ch0
            lux = max(0.0, ((ch0 - ir) * (1-ir_ratio)) / cpl) * meta["tsl_scale"]
        rows.append({
            "generation": meta["generation"],
            "session_id": meta["session_id"],
            "sequence": (base_seq + i) & 0xffffffff,
            "timestamp_monotonic_us": base_us + delta * 1000,
            "bme_valid": bme,
            "temp_c": t/100 if bme else None,
            "rh_percent": rh/100 if bme else None,
            "pressure_pa": pressure if bme else None,
            "tsl_valid": tsl,
            "tsl_saturated": saturated,
            "tsl_ch0": ch0 if tsl else None,
            "tsl_ir": ir if tsl else None,
            "tsl_lux_relative": lux,
            "ltr_valid": ltr,
            "ltr_als": als if ltr else None,
            "ltr_uv": uv if ltr else None,
            "ltr_lux": als*0.2 if ltr else None,
            "ltr_uvi": uv*12/1150 if ltr else None,
            "sgp_valid": sgp,
            "voc_ready": ready,
            "sgp_sraw": raw_voc if sgp else None,
            "voc_index": index if ready else None,
            "flags": f,
        })
    return rows, None

def inspect_headers(image):
    """Classify the partition independently from record count.

    Header-only sectors occur when the first ten 1 Hz samples have not yet
    been committed into a full 256-byte program page.
    """
    info = dict(
        sectors=len(image)//SECTOR,
        erased_sectors=0,
        valid_headers=0,
        header_only_sectors=0,
        invalid_or_unknown_sectors=0,
    )
    erased_page = b"\xff" * PAGE
    erased_sector = b"\xff" * SECTOR
    for at in range(0, len(image)-SECTOR+1, SECTOR):
        sector = image[at:at+SECTOR]
        if sector == erased_sector:
            info["erased_sectors"] += 1
        elif sector_header(sector[:HEADER]) is not None:
            info["valid_headers"] += 1
            if sector[PAGE:PAGE*2] == erased_page:
                info["header_only_sectors"] += 1
        else:
            info["invalid_or_unknown_sectors"] += 1
    return info


def scan(image):
    """Scans complete sectors in chronological generation order."""
    sectors, problems = [], []
    for off in range(0, len(image)-SECTOR+1, SECTOR):
        meta = sector_header(image[off:off+HEADER])
        if meta is not None:
            sectors.append((meta["generation"], off, meta))
        elif image[off:off+SECTOR] != b"\xff" * SECTOR:
            problems.append(("sector", off, "non_erased_without_valid_header"))
    if not sectors:
        return [], problems
    newest = sectors[0][0]
    for gen, _, _ in sectors[1:]:
        if ((gen - newest + 2**31) % 2**32) - 2**31 > 0:
            newest = gen
    sectors.sort(key=lambda item: (newest-item[0]) & 0xffffffff, reverse=True)
    rows = []
    for _, off, meta in sectors:
        for page_num in range(1, 16):
            start = off + page_num * PAGE
            decoded, error = page_rows(image[start:start+PAGE], meta)
            if error:
                problems.append(("page", start, error))
                # Never resynchronize mid-sector after a torn/corrupt page.
                break
            if not decoded:  # unused page terminates this sector
                break
            rows.extend(decoded)
    return rows, problems

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path, help="12 MiB read-only BOOTSEL flash partition dump")
    ap.add_argument("--csv", type=Path, help="write decoded CSV")
    args = ap.parse_args()
    image = args.input.read_bytes()
    if len(image) != PARTITION_SIZE:
        ap.error(f"expected exactly {PARTITION_SIZE} bytes (12 MiB), got {len(image)}")
    rows, problems = scan(image)
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as fp:
            out = csv.DictWriter(fp, fieldnames=FIELDS)
            out.writeheader()
            out.writerows(rows)
    info = inspect_headers(image)
    sessions = len(set(x["session_id"] for x in rows))
    print(
        f"Flash: {info['sectors']} sectors; "
        f"valid ELOG headers={info['valid_headers']}; "
        f"header-only={info['header_only_sectors']}; "
        f"fully erased={info['erased_sectors']}; "
        f"invalid/unknown={info['invalid_or_unknown_sectors']}"
    )
    print(f"Decoded: {len(rows)} samples, {sessions} data-bearing sessions, {len(problems)} warnings")
    if not rows and info["valid_headers"]:
        print("INFO: valid ELOG sector(s) exist but no complete CRC-valid record page is present.")
    elif not rows and info["erased_sectors"] == info["sectors"]:
        print("INFO: entire logger partition is erased (all 0xFF); no ELOG metadata was written.")
    elif not rows and not problems:
        print("INFO: no samples recovered; investigate image age and logger state.")
    for kind, at, reason in problems[:20]:
        print(f"WARNING: {kind} +0x{at:06x}: {reason}", file=sys.stderr)
    if len(problems) > 20:
        print(f"... {len(problems)-20} more warnings", file=sys.stderr)
    if not rows and problems:
        return 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
