import importlib.util
import ctypes
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import struct
import unittest

ROOT = get_repo_root()
spec = importlib.util.spec_from_file_location(
    "linux_vgpu_info", ROOT / "scripts/decode-linux-vgpu-info.py")
linux_vgpu_info = importlib.util.module_from_spec(spec)
spec.loader.exec_module(linux_vgpu_info)


def sample_page():
    data = bytearray(linux_vgpu_info.INFO_PAGE_SIZE)
    struct.pack_into("<III", data, 0, linux_vgpu_info.MAGIC, 1, 7)
    struct.pack_into("<Q", data, 0x10, 1)
    struct.pack_into("<QQ", data, 0x18, 1 << 30, 0x43000000)
    struct.pack_into("<QI", data, 0x28, 0x100000000, 0x40000000)
    struct.pack_into("<I", data, 0x30, 1)
    struct.pack_into("<QQ", data, 0x38, 0x100000000, 0x40000000)
    struct.pack_into("<QI", data, 0x438, 0x43000000, 1)
    struct.pack_into("<QQ", data, 0x448, 0, 0x43000000)
    struct.pack_into("<QI", data, 0x848, 0x7737EF000, 0x800000)
    struct.pack_into("<QI", data, 0x858, 0x773FEF000, 0x100000)
    return data


class LinuxVgpuInfoDecoderTests(unittest.TestCase):
    def test_typed_v1_records_and_tail_use_the_observed_extended_stride(self):
        page = bytearray(4096)
        struct.pack_into('<III', page, 0, linux_vgpu_info.MAGIC, 1, 6)
        struct.pack_into('<QI', page, 0x28, 0x300000, 2)
        struct.pack_into('<QQQ', page, 0x38, 0x600000000, 0x100000, 1)
        struct.pack_into('<QQQ', page, 0x50, 0x1ae000000, 0x200000, 3)
        struct.pack_into('<QI', page, 0x638, 0x4000000, 1)
        struct.pack_into('<QQQ', page, 0x648, 0x769fef000, 0x4000000, 4)
        struct.pack_into('<Q', page, 0xc68, 0x58)
        struct.pack_into('<II', page, 0xc78, 2560, 1600)
        result = linux_vgpu_info.decode(page, 24)
        self.assertEqual(result['segments']['vgpu']['records'][1]['base_raw'], '0x1ae000000')
        self.assertEqual(result['segments']['vgpu']['records'][1]['flags_raw'], '0x3')
        self.assertEqual(result['segments']['vvpu']['records'][0]['base_raw'], '0x769fef000')
        self.assertEqual(result['extension']['max_resolution_width'], 2560)
        self.assertEqual(result['fw_heap_base_raw'], '0x0')
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            linux_vgpu_info.decode(page[:0x8a0], 24)
        struct.pack_into('<Q', page, 0x28, 0x400000)
        with self.assertRaisesRegex(ValueError, 'aggregate'):
            linux_vgpu_info.decode(page, 24)

    def test_decoder_offsets_match_the_official_guest_header_layout(self):
        class MemSegment(ctypes.Structure):
            _fields_ = [("base", ctypes.c_uint64), ("size", ctypes.c_uint64)]

        class VmSegmentInfo(ctypes.Structure):
            _fields_ = [
                ("size", ctypes.c_uint64),
                ("segment_cnt", ctypes.c_uint32),
                ("mem_segment", MemSegment * 64),
            ]

        class VgpuInfoExt(ctypes.Structure):
            _fields_ = [
                ("padding", ctypes.c_uint64),
                ("max_resolution_width", ctypes.c_uint32),
                ("max_resolution_height", ctypes.c_uint32),
                ("max_encode_num", ctypes.c_uint32),
                ("max_decode_num", ctypes.c_uint32),
                ("pb_fl_va_base", ctypes.c_uint64),
                ("pb_fl_pa_base", ctypes.c_uint64),
                ("pb_fl_size", ctypes.c_uint32),
                ("mpc_core_nums", ctypes.c_uint32),
            ]

        class VgpuInfo(ctypes.Structure):
            _fields_ = [
                ("magic", ctypes.c_uint32),
                ("version", ctypes.c_uint32),
                ("osid", ctypes.c_uint32),
                ("flag", ctypes.c_uint64),
                ("vm_mem_size", ctypes.c_uint64),
                ("vm_bar2_actual_mem_size", ctypes.c_uint64),
                ("segment_info", VmSegmentInfo * 2),
                ("fw_heap_base", ctypes.c_uint64),
                ("fw_heap_size", ctypes.c_uint32),
                ("mmu_heap_base", ctypes.c_uint64),
                ("mmu_heap_size", ctypes.c_uint32),
                ("ext_size", ctypes.c_uint64),
                ("ext_info", VgpuInfoExt),
            ]

        self.assertEqual(VgpuInfo.segment_info.offset, 0x28)
        self.assertEqual(VmSegmentInfo.mem_segment.offset, 0x10)
        self.assertEqual(VgpuInfo.fw_heap_base.offset, 0x848)
        self.assertEqual(VgpuInfo.mmu_heap_base.offset, 0x858)
        self.assertEqual(VgpuInfo.ext_size.offset, 0x868)
        self.assertEqual(VgpuInfoExt.max_resolution_width.offset, 0x08)
        self.assertEqual(ctypes.sizeof(VgpuInfo), linux_vgpu_info.INFO_PAGE_SIZE)

    def test_decodes_v1_heap_and_segment_fields_without_translating_addresses(self):
        result = linux_vgpu_info.decode(sample_page())
        self.assertEqual(result["osid"], 7)
        self.assertTrue(result["flag_bits"]["enable_vpu"])
        self.assertEqual(result["fw_heap_base_raw"], "0x7737ef000")
        self.assertEqual(result["fw_heap_size_bytes"], 0x800000)
        self.assertEqual(result["segments"]["vvpu"]["aggregate_size_bytes"], 0x43000000)
        self.assertEqual(result["segments"]["vvpu"]["records"][0]["base_raw"], "0x0")
        self.assertIn("does not infer", result["address_domain_note"])

    def test_rejects_the_windows_v2_page_layout(self):
        data = sample_page()
        struct.pack_into("<I", data, 4, 2)
        with self.assertRaisesRegex(ValueError, "Expected Linux info-page v1"):
            linux_vgpu_info.decode(data)

    def test_rejects_invalid_segment_count(self):
        data = sample_page()
        struct.pack_into("<I", data, 0x30, 65)
        with self.assertRaisesRegex(ValueError, "segment count"):
            linux_vgpu_info.decode(data)

    def test_rejects_truncated_or_wrapped_segment_records(self):
        with self.assertRaisesRegex(ValueError, "Truncated"):
            linux_vgpu_info.decode(sample_page()[:0x800])
        data = sample_page()
        struct.pack_into("<QQ", data, 0x38, (1 << 64) - 8, 16)
        with self.assertRaisesRegex(ValueError, "wraps"):
            linux_vgpu_info.decode(data)


if __name__ == "__main__":
    unittest.main()
