import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("firmware", ROOT / "scripts/extract-firmware.py")
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)


class FirmwareLoaderTests(unittest.TestCase):
    def setUp(self):
        self.blob = bytearray(128)
        struct.pack_into("<I", self.blob, 8, 0x10)
        struct.pack_into("<HHIII", self.blob, 0x10, 0, 20, 0xFFFFFFFF, 0xC0001000, 0x30)
        struct.pack_into("<HH", self.blob, 0x30, 0, 10)
        self.blob[0x34:0x38] = b"test"
        self.segments = [(2, 1, 0x40000000, 0x40000, 0x11000, 0)]

    def test_copy_translation_and_size(self):
        record = firmware.parse_loader(self.blob, self.segments)[0]
        self.assertEqual(record["size"], 4)
        self.assertEqual(record["destination"], {"region": "firmware", "offset": "0x1000"})

    def test_bootstrap_window_is_distinct_from_gpu_code(self):
        struct.pack_into("<I", self.blob, 0x18, 0x80001000)
        record = firmware.parse_loader(self.blob, self.segments)[0]
        self.assertEqual(record["destination"], {"region": "bootstrap", "offset": "0x2a160"})

    def test_cyclic_chain_rejected(self):
        struct.pack_into("<I", self.blob, 0x14, 0x10)
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            firmware.parse_loader(self.blob, self.segments)

    def test_truncated_payload_rejected(self):
        struct.pack_into("<H", self.blob, 0x32, 0xFFFF)
        with self.assertRaisesRegex(ValueError, "out of bounds"):
            firmware.parse_loader(self.blob, self.segments)

    def test_unmapped_target_rejected(self):
        struct.pack_into("<I", self.blob, 0x18, 0x50000000)
        with self.assertRaisesRegex(ValueError, "No translation"):
            firmware.parse_loader(self.blob, self.segments)

    def test_segment_boundary_crossing_rejected(self):
        struct.pack_into("<I", self.blob, 0x18, 0xC0010FFF)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            firmware.parse_loader(self.blob, self.segments)

    def test_loader_image_rejects_destination_outside_allocation(self):
        segments = [(2, 1, 0x40000000, 0x40000, 0x11000, 0x800000)]
        with self.assertRaisesRegex(ValueError, "exceeds Guest allocation"):
            firmware.build_guest_loader_image(self.blob, segments, 0xe1c0000000)

    def test_loader_image_rejects_invalid_va_and_mode(self):
        for va, mode in [(1, 0), (-4096, 0), (1 << 40, 0),
                         ((1 << 40) - 4096, 0), (0xe1c0000000, 2)]:
            with self.subTest(va=va, mode=mode), self.assertRaises(ValueError):
                firmware.build_guest_loader_image(self.blob, self.segments, va, mode)


if __name__ == "__main__":
    unittest.main()
