"""HTTP protocol and fail-closed backup tests; no attached RP2350 required."""
import json
import struct
import sys
import tempfile
import threading
import unittest
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import envlog_backup as backup
import envlog_parse as elog


def sample_sector():
    sec = bytearray(b"\xff" * elog.SECTOR)
    sec[:4] = b"ELOG"
    struct.pack_into("<HHII", sec, 4, 1, 256, 17, 9)
    struct.pack_into("<II", sec, 16, 4 * 1024 * 1024, elog.PARTITION_SIZE)
    struct.pack_into("<HHHH", sec, 24, 24, 256, 4096, 10)
    struct.pack_into("<HH", sec, 32, 408, 2700)
    sec[36:40] = bytes((0x10, 0x22, 1, 1))
    struct.pack_into("<QQ", sec, 40, 1793529615000000, 10000000)
    sec[56] = 1
    struct.pack_into("<I", sec, 252, zlib.crc32(sec[:252]) & 0xffffffff)
    page = bytearray(b"\xff" * 256)
    struct.pack_into("<QI", page, 0, 11000000, 100)
    rec = bytearray(24)
    struct.pack_into("<HhH", rec, 0, 0, 2550, 6320)
    rec[6:9] = (100900).to_bytes(3, "little")
    rec[-1] = 0x01
    page[16:40] = rec
    struct.pack_into("<I", page, 12, zlib.crc32(page[:12] + page[16:]) & 0xffffffff)
    sec[256:512] = page
    return bytes(sec)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        fixture = self.server.fixture
        sector = fixture["sector"]
        crc = struct.unpack_from("<I", sector, 252)[0]
        if self.path == "/api/v1/log/status":
            body = json.dumps({
                "schema": "rp2350.envlog.export.v1", "version": 1,
                "sectorBytes": 4096, "sectorCount": 3072,
                "partitionBytes": 12582912, "manifestPage": 64,
                "validSectors": 1, "unknownHeaders": 0,
            }).encode()
        elif self.path.startswith("/api/v1/log/list/"):
            start = int(self.path.rsplit("/", 1)[-1])
            if start == 0:
                fixture["manifest_zero_calls"] += 1
            current_crc = crc
            if (fixture["recycle"] and fixture["manifest_zero_calls"] >= 2
                    and start == 0):
                current_crc ^= 1
            body = json.dumps({
                "schema": "rp2350.envlog.list.v1", "start": start,
                "entries": [[0, 17, current_crc]] if start == 0 else [],
            }).encode()
        elif self.path == f"/api/v1/log/sector/0/17/{crc}":
            body = sector if not fixture["bad_page"] else (
                sector[:290] + bytes([sector[290] ^ 0x01]) + sector[291:])
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.httpd.fixture = {"sector": sample_sector(), "recycle": False,
                              "bad_page": False, "manifest_zero_calls": 0}
        self.thread = threading.Thread(target=self.httpd.serve_forever,
                                       daemon=True)
        self.thread.start()
        self.client = backup.Client(f"http://127.0.0.1:{self.httpd.server_port}")

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join()

    def test_verified_capture_and_persist(self):
        image, summary = backup.capture(self.client)
        self.assertEqual(len(image), 12582912)
        self.assertEqual(image[:4096], self.httpd.fixture["sector"])
        self.assertEqual(summary["samples"], 1)
        self.assertEqual(summary["utc_samples"], 1)
        self.assertEqual(summary["parser_warnings"], 0)
        with tempfile.TemporaryDirectory() as root:
            binary, meta = backup.save_verified(image, summary, root)
            self.assertEqual(binary.stat().st_size, 12582912)
            self.assertEqual(json.loads(meta.read_text())["sha256"], summary["sha256"])

    def test_recycle_fails_closed(self):
        self.httpd.fixture["recycle"] = True
        with self.assertRaisesRegex(backup.BackupError, "recycled"):
            backup.capture(self.client)

    def test_page_crc_fails_closed(self):
        self.httpd.fixture["bad_page"] = True
        with self.assertRaisesRegex(backup.BackupError, "validation failed"):
            backup.capture(self.client)

    def test_public_url_rejected(self):
        with self.assertRaises(backup.BackupError):
            backup.Client("https://sensor.cafefeed.idv.tw")

    def test_download_uses_only_get(self):
        self.assertEqual(self.httpd.fixture["manifest_zero_calls"], 0)
        backup.capture(self.client)
        self.assertEqual(self.httpd.fixture["manifest_zero_calls"], 2)


if __name__ == "__main__":
    unittest.main()
