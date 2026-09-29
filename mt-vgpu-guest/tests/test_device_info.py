import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("info", ROOT / "scripts/decode-device-info.py")
info = importlib.util.module_from_spec(spec)
spec.loader.exec_module(info)


class DeviceInfoTests(unittest.TestCase):
    def setUp(self):
        self.data = bytearray((ROOT / "reports/device-info.bin").read_bytes())

    def test_real_host_response_and_address_domains(self):
        result = info.decode(self.data)
        self.assertEqual((result["version"], result["osid"]), (2, 4))
        self.assertEqual(result["vm_memory_size_bytes"], 1024**3)
        self.assertEqual(result["segment_count"], 6)
        self.assertEqual(result["segments"][2]["bar2_offset"], "0x3f000000")
        self.assertEqual(result["segments"][5]["shared_bar2_offset"], "0x43000000")
        self.assertEqual(result["mapped_segment_end"], "0x43000000")

    def test_reject_bad_signature_version_and_truncation(self):
        for offset, value in [(0, 0), (4, 1)]:
            with self.subTest(offset=offset):
                data = bytearray(self.data)
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    info.decode(data)
        with self.assertRaises(ValueError):
            info.decode(self.data[:0xCC7])

    def test_segment_count_cannot_read_extension_as_memory(self):
        struct.pack_into("<I", self.data, 0xC50, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            info.decode(self.data)

    def test_mapped_range_must_fit_reported_size(self):
        struct.pack_into("<Q", self.data, 0x20, 1024)
        with self.assertRaises(ValueError):
            info.decode(self.data)

    def test_address_overflow_rejected(self):
        struct.pack_into("<Q", self.data, 0x28, (1 << 64) - 1)
        with self.assertRaises(ValueError):
            info.decode(self.data)


if __name__ == "__main__":
    unittest.main()
