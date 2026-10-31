#!/usr/bin/env python3
"""Golden-vector and recovery tests (no RP2350 hardware required)."""
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import envlog_parse as log  # noqa: E402


def sample(delta, flags=0x2f, seq_data=None):
    # Record fields: delta 2 + BME 7 + TSL 4 + LTR 6 + VOC 4 + flags 1
    data = bytearray(24)
    struct.pack_into("<HhH", data, 0, delta, 2451, 6134)
    data[6:9] = (100123).to_bytes(3, "little")
    struct.pack_into("<HH", data, 9, 2100, 340)
    data[13:16] = (3180).to_bytes(3, "little")
    data[16:19] = (0).to_bytes(3, "little")
    struct.pack_into("<HHB", data, 19, 30050, 95, flags)
    return bytes(data)


def page(base=12345000, seq=100, records=10, corrupt=False):
    b = bytearray(b"\xff" * 256)
    struct.pack_into("<QI", b, 0, base, seq)
    for i in range(records):
        b[16 + 24*i:16 + 24*(i+1)] = sample(i*1000)
    crc = zlib.crc32(b[:12] + b[16:]) & 0xffffffff
    struct.pack_into("<I", b, 12, crc)
    if corrupt:
        b[33] ^= 1
    return bytes(b)


def sector_header(gen=1, session=1):
    b = bytearray(b"\xff" * 256)
    b[:4] = b"ELOG"
    struct.pack_into("<HHII", b, 4, 1, 256, gen, session)
    struct.pack_into("<II", b, 16, 4*1024*1024, 12*1024*1024)
    struct.pack_into("<HHHH", b, 24, 24, 256, 4096, 10)
    struct.pack_into("<HH", b, 32, 408, 2700)
    b[36:40] = bytes((0x10, 0x22, 1, 0))
    struct.pack_into("<I", b, 252, zlib.crc32(b[:252]) & 0xffffffff)
    return bytes(b)


class FormatTests(unittest.TestCase):
    def test_layout(self):
        self.assertEqual(len(sample(0)), 24)
        self.assertEqual(len(page()), 256)
        self.assertEqual(len(sector_header()), 256)
        self.assertIsNotNone(log.sector_header(sector_header()))
        self.assertIsNone(log.sector_header(sector_header()[:252] + b"\0\0\0\0"))

    def test_flash_inspection_erased_vs_header_only(self):
        erased = b"\xff" * 4096
        info = log.inspect_headers(erased)
        self.assertEqual(info["erased_sectors"], 1)
        self.assertEqual(info["valid_headers"], 0)
        self.assertEqual(info["header_only_sectors"], 0)
        self.assertEqual(log.scan(erased), ([], []))

        header_only = bytearray(erased)
        header_only[:256] = sector_header()
        info = log.inspect_headers(header_only)
        self.assertEqual(info["valid_headers"], 1)
        self.assertEqual(info["header_only_sectors"], 1)
        self.assertEqual(log.scan(bytes(header_only)), ([], []))

    def test_non_erased_without_header_is_reported(self):
        damaged = bytearray(b"\xff" * 4096)
        damaged[300] = 0
        info = log.inspect_headers(damaged)
        self.assertEqual(info["valid_headers"], 0)
        self.assertEqual(info["invalid_or_unknown_sectors"], 1)
        rows, issues = log.scan(bytes(damaged))
        self.assertEqual(rows, [])
        self.assertEqual(issues[0][2], "non_erased_without_valid_header")

    def test_single_page_roundtrip(self):
        meta = log.sector_header(sector_header())
        rows, error = log.page_rows(page(), meta)
        self.assertIsNone(error)
        self.assertEqual(len(rows), 10)
        self.assertEqual(rows[0]["timestamp_monotonic_us"], 12345000)
        self.assertEqual(rows[9]["timestamp_monotonic_us"], 21345000)
        self.assertEqual(rows[9]["sequence"], 109)
        self.assertEqual(rows[0]["pressure_pa"], 100123)
        self.assertEqual(rows[0]["ltr_als"], 3180)
        self.assertAlmostEqual(rows[0]["temp_c"], 24.51)
        self.assertTrue(rows[0]["voc_ready"])

    def test_partial_page(self):
        meta = log.sector_header(sector_header())
        rows, error = log.page_rows(page(records=3), meta)
        self.assertIsNone(error)
        self.assertEqual(len(rows), 3)

    def test_corruption_rejected(self):
        meta = log.sector_header(sector_header())
        rows, error = log.page_rows(page(corrupt=True), meta)
        self.assertEqual(rows, [])
        self.assertEqual(error, "crc32_mismatch")

    def test_erased_page_and_flags(self):
        meta = log.sector_header(sector_header())
        self.assertEqual(log.page_rows(b"\xff"*256, meta), ([], None))
        bad = bytearray(page())
        bad[16 + 23] = 0x10  # TSL saturated without TSL valid
        struct.pack_into("<I", bad, 12, zlib.crc32(bad[:12]+bad[16:])&0xffffffff)
        rows, error = log.page_rows(bad, meta)
        self.assertEqual(rows, [])
        self.assertEqual(error, "contradictory_flags")

    def test_generation_sort_and_missing_utc(self):
        image = bytearray(b"\xff"*(4096*3))
        # Newest generation wraps 0xffffffff -> 0.
        image[0:256] = sector_header(0xffffffff, 11)
        image[256:512] = page(1000000, 200)
        image[4096:4352] = sector_header(0, 12)
        image[4352:4608] = page(2000000, 1)
        rows, issues = log.scan(bytes(image))
        self.assertEqual(len(issues), 0)
        self.assertEqual(len(rows), 20)
        self.assertEqual(rows[0]["session_id"], 11)
        self.assertEqual(rows[-1]["session_id"], 12)
        self.assertFalse(log.sector_header(sector_header())["utc_valid"])

    def test_torn_page_retains_earlier_data(self):
        image = bytearray(b"\xff"*4096)
        image[0:256] = sector_header()
        image[256:512] = page(records=10)
        image[512:768] = page(seq=110, corrupt=True)
        rows, issues = log.scan(bytes(image))
        self.assertEqual(len(rows), 10)
        self.assertEqual(issues[0][2], "crc32_mismatch")


if __name__ == "__main__":
    unittest.main()
