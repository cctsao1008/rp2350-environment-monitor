import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "ntp_time_probe",
    Path(__file__).resolve().parents[1] / "tools" / "ntp_time_probe.py")
ntp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ntp)


def make_reply(t1, t2, t3, li=0, mode=4, stratum=2):
    sent=bytearray(48)
    sent[0]=(4 << 3) | 3
    sent[40:48]=ntp._to_ntp(t1)
    packet=bytearray(48)
    packet[0]=(li<<6)|(4<<3)|mode
    packet[1]=stratum
    packet[8:12]=(6554).to_bytes(4,"big")  # 0.1 s dispersion
    packet[24:32]=sent[40:48]
    packet[32:40]=ntp._to_ntp(t2)
    packet[40:48]=ntp._to_ntp(t3)
    return bytes(sent),bytes(packet)


class NtpTests(unittest.TestCase):
    def test_valid_rfc5905_offset_and_delay(self):
        t1=1700000000.0
        sent,pkt=make_reply(t1,t1+0.060,t1+0.061)
        got=ntp.parse_response(pkt,sent,t1,t1+0.100)
        self.assertAlmostEqual(got["offset_ms"],10.5,delta=.02)
        self.assertAlmostEqual(got["round_trip_ms"],99,delta=.02)
        self.assertAlmostEqual(got["root_dispersion_ms"],100.006,delta=.03)
        self.assertTrue(got["estimated_uncertainty_ms"]>100)
        self.assertEqual(got["stratum"],2)
        self.assertTrue(got["utc"].endswith("+00:00"))

    def test_origin_mismatch_is_rejected(self):
        t=1700000000.0
        sent,pkt=make_reply(t,t+.06,t+.061)
        damaged=bytearray(pkt);damaged[24]^=1
        with self.assertRaisesRegex(ntp.NtpError,"originate"):
            ntp.parse_response(bytes(damaged),sent,t,t+.1)

    def test_unsynchronized_li_and_stratum_rejected(self):
        t=1700000000.0
        for li,stratum in [(3,2),(0,0),(0,16)]:
            sent,pkt=make_reply(t,t+.06,t+.061,li=li,stratum=stratum)
            with self.assertRaises(ntp.NtpError):
                ntp.parse_response(pkt,sent,t,t+.1)

    def test_wrong_mode_rejected(self):
        t=1700000000.0
        sent,pkt=make_reply(t,t+.06,t+.061,mode=3)
        with self.assertRaisesRegex(ntp.NtpError,"mode"):
            ntp.parse_response(pkt,sent,t,t+.1)

    def test_large_error_or_delay_rejected(self):
        t=1700000000.0
        sent,pkt=make_reply(t,t+.06,t+.061)
        with self.assertRaisesRegex(ntp.NtpError,"delay"):
            ntp.parse_response(pkt,sent,t,t+4)

    def test_round_trip_negative_delay_rejected(self):
        t=1700000000.0
        sent,pkt=make_reply(t,t+.02,t+.6)
        with self.assertRaisesRegex(ntp.NtpError,"delay"):
            ntp.parse_response(pkt,sent,t,t+.2)


if __name__ == "__main__":
    unittest.main()
