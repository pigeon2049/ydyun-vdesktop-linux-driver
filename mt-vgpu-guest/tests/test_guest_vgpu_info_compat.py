import ctypes
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "patches/mtgpu_vgpu_info_compat.c"
ADDR_WRAPPER = ROOT / "patches/mtgpu_vgpu_addr_compat.c"
ADDR_STUB = ROOT / "tests/vgpu_addr_v1_stub.c"
CAPTURE = ROOT / "reports/device-info.bin"
PAGE_SIZE = 4096
MAGIC = 0xAA557491
FW_BASE = 0x771FEF000
FW_BYTES = 0x800000
BAR2_BASE = 0x800000000
BAR2_SIZE = 0x43000000
PCI_BAR2_SIZE = 0x400000000


def blank_page(version):
    page = bytearray(PAGE_SIZE)
    struct.pack_into("<II", page, 0, MAGIC, version)
    return page


def add_v2_segment(page, base, size, flags, index=0):
    offset = 0x28 + index * 24
    struct.pack_into("<QQQ", page, offset, base, size, flags)


class GuestVgpuInfoCompatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which("cc") is None:
            raise unittest.SkipTest("a C compiler is required for the ABI helper test")
        cls.tempdir = tempfile.TemporaryDirectory(prefix="mtgpu-info-compat-")
        library = Path(cls.tempdir.name) / "libmtgpu_info_compat.so"
        subprocess.run([
            "cc", "-shared", "-fPIC", "-DMTGPU_COMPAT_USERSPACE_TEST",
            str(SOURCE), str(ADDR_WRAPPER), str(ADDR_STUB), "-o", str(library),
        ], check=True, capture_output=True, text=True)
        cls.library = ctypes.CDLL(str(library))
        cls.helper = cls.library.mtgpu_guest_fw_heap_base_compat
        cls.helper.argtypes = [ctypes.c_void_p]
        cls.helper.restype = ctypes.c_uint64
        cls.share_helper = cls.library.mtgpu_guest_vpu_share_mem_addr_compat
        cls.share_helper.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        cls.share_helper.restype = ctypes.c_void_p
        cls.share_base_helper = cls.library.mtgpu_guest_vpu_share_mem_base_compat
        cls.share_base_helper.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                          ctypes.c_void_p]
        cls.share_base_helper.restype = ctypes.c_uint64
        cls.translate = cls.library.mtgpu_guest_v2_gdpa_to_host_compat
        cls.translate.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                                  ctypes.c_uint64, ctypes.c_uint64]
        cls.translate.restype = ctypes.c_uint64
        cls.dispatch = cls.library.GuestDevicePAddrToHostDevicePAddr
        cls.dispatch.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        cls.dispatch.restype = ctypes.c_uint64

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tempdir"):
            cls.tempdir.cleanup()

    def decode(self, page):
        buffer = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        return self.helper(buffer)

    def translate_v2(self, page, gdpa, bar2_base=BAR2_BASE,
                     bar2_size=BAR2_SIZE):
        buffer = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        return self.translate(buffer, bar2_base, bar2_size, gdpa)

    def dispatch_page(self, page, gdpa, bar2_base=BAR2_BASE,
                      mmu_size=0, mmu_card_base=PCI_BAR2_SIZE):
        root = ctypes.create_string_buffer(0x80 + 8)
        device = ctypes.create_string_buffer(0x78 + 8)
        platform = ctypes.create_string_buffer(0x70 + 8)
        info = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        ctypes.c_uint64.from_address(ctypes.addressof(root) + 0x78).value = ctypes.addressof(device)
        ctypes.c_uint64.from_address(ctypes.addressof(device) + 0x08).value = ctypes.addressof(platform)
        ctypes.c_uint64.from_address(ctypes.addressof(platform) + 0x08).value = bar2_base
        ctypes.c_uint64.from_address(ctypes.addressof(platform) + 0x40).value = mmu_size
        ctypes.c_uint64.from_address(ctypes.addressof(platform) + 0x48).value = mmu_card_base
        ctypes.c_uint64.from_address(ctypes.addressof(platform) + 0x70).value = ctypes.addressof(info)
        return self.dispatch(root, gdpa)

    def share_address(self, page, bar2_base=BAR2_BASE,
                      bar2_size=PCI_BAR2_SIZE):
        mtdev = ctypes.create_string_buffer(0x1138 + 8)
        share_mem = ctypes.create_string_buffer(0x28)
        info = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x78).value = bar2_base
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x80).value = bar2_size
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x1138).value = ctypes.addressof(info)
        returned = self.share_helper(share_mem, mtdev)
        self.assertEqual(returned, ctypes.addressof(share_mem))
        return ctypes.c_uint64.from_address(ctypes.addressof(share_mem) + 0x10).value

    def share_size(self, page, bar2_base=BAR2_BASE,
                   bar2_size=PCI_BAR2_SIZE):
        mtdev = ctypes.create_string_buffer(0x1138 + 8)
        share_mem = ctypes.create_string_buffer(0x28)
        info = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x78).value = bar2_base
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x80).value = bar2_size
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x1138).value = ctypes.addressof(info)
        returned = self.share_helper(share_mem, mtdev)
        self.assertEqual(returned, ctypes.addressof(share_mem))
        return ctypes.c_uint64.from_address(ctypes.addressof(share_mem) + 0x20).value

    def vpu_guest_map_base(self, page, bar2_base=BAR2_BASE,
                           bar2_size=PCI_BAR2_SIZE, published_base=None,
                           published_size=None, page_after_publish=None):
        mtdev = ctypes.create_string_buffer(0x1138 + 8)
        share_mem = ctypes.create_string_buffer(0x28)
        info = (ctypes.c_ubyte * len(page)).from_buffer_copy(page)
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x78).value = bar2_base
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x80).value = bar2_size
        ctypes.c_uint64.from_address(ctypes.addressof(mtdev) + 0x1138).value = ctypes.addressof(info)
        self.share_helper(share_mem, mtdev)
        if page_after_publish is not None:
            if len(page_after_publish) != len(page):
                raise ValueError("replacement info page must preserve its allocation size")
            ctypes.memmove(ctypes.addressof(info), bytes(page_after_publish), len(page))
        if published_base is not None:
            ctypes.c_uint64.from_address(ctypes.addressof(share_mem) + 0x10).value = published_base
        if published_size is not None:
            ctypes.c_uint64.from_address(ctypes.addressof(share_mem) + 0x20).value = published_size
        return self.share_base_helper(info, share_mem, mtdev)

    def test_v1_reads_linux_fw_heap_base(self):
        page = blank_page(1)
        struct.pack_into("<QI", page, 0x848, FW_BASE, FW_BYTES)
        self.assertEqual(self.decode(page), FW_BASE)

    def test_v1_rejects_small_heap_and_wrapping_base(self):
        page = blank_page(1)
        struct.pack_into("<QI", page, 0x848, FW_BASE, FW_BYTES - 1)
        self.assertEqual(self.decode(page), 0)
        struct.pack_into("<QI", page, 0x848, (1 << 64) - 0x1000, FW_BYTES)
        self.assertEqual(self.decode(page), 0)

    def test_rejects_null_info_page(self):
        self.assertEqual(self.helper(None), 0)

    def test_v2_uses_the_unique_firmware_flag_segment(self):
        page = blank_page(2)
        struct.pack_into("<I", page, 0xC50, 2)
        add_v2_segment(page, 0x100000000, 0x5000000, 1)
        add_v2_segment(page, FW_BASE, FW_BYTES, 4, 1)
        self.assertEqual(self.decode(page), FW_BASE)

    def test_v2_max_segment_table_fits_the_expanded_info_page(self):
        page = blank_page(2)
        struct.pack_into("<Q", page, 0x10, 0x80)
        struct.pack_into("<I", page, 0xC50, 129)
        add_v2_segment(page, 0x43000000, 0x800000, 0x24, 128)
        self.assertEqual(self.decode(page), 0x43000000)
        self.assertEqual(self.share_address(page), BAR2_BASE + 0x43000000)
        self.assertEqual(self.share_size(page), 0x200000)

    def test_captured_windows_v2_page_resolves_expected_fw_base(self):
        page = CAPTURE.read_bytes()
        self.assertEqual(len(page), PAGE_SIZE)
        self.assertEqual(self.decode(page), FW_BASE)

    def test_v1_shared_memory_address_keeps_the_linux_field_value(self):
        page = blank_page(1)
        struct.pack_into("<Q", page, 0x448, 0x12345678000)
        self.assertEqual(self.share_address(page), 0x12345678000)
        self.assertEqual(self.share_size(page), 0x8000)

    def test_captured_v2_shared_memory_address_is_bar2_base_plus_shared_offset(self):
        page = CAPTURE.read_bytes()
        self.assertEqual(self.share_address(page), BAR2_BASE + 0x43000000)
        self.assertEqual(self.share_size(page), 0x200000)

    def test_v2_vpu_ring_maps_at_published_shared_segment(self):
        page = CAPTURE.read_bytes()
        self.assertEqual(self.vpu_guest_map_base(page), BAR2_BASE + 0x43000000)

    def test_vpu_ring_keeps_legacy_v1_mapping_and_rejects_bad_v2_mapping(self):
        page = blank_page(1)
        struct.pack_into("<Q", page, 0x448, 0x12345678000)
        self.assertEqual(self.vpu_guest_map_base(page), 0)

        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<Q", page, 0x10,
                         struct.unpack_from("<Q", page, 0x10)[0] & ~0x80)
        self.assertEqual(self.vpu_guest_map_base(page), 0)

        page = CAPTURE.read_bytes()
        self.assertEqual(self.vpu_guest_map_base(page, bar2_size=0x431FFFFF), 0)
        self.assertEqual(self.vpu_guest_map_base(page, published_size=0x1FFFFF), 0)
        self.assertEqual(self.vpu_guest_map_base(
            page, published_base=BAR2_BASE + PCI_BAR2_SIZE - 0x1000), 0)
        self.assertEqual(self.vpu_guest_map_base(
            page, published_base=BAR2_BASE + 0x100000), 0)
        self.assertEqual(self.vpu_guest_map_base(
            page, published_size=0x400000), 0)

        changed = bytearray(page)
        struct.pack_into("<Q", changed, 0x28 + 5 * 24, 0x42000000)
        self.assertEqual(self.vpu_guest_map_base(
            page, page_after_publish=changed), 0)
        ambiguous = bytearray(page)
        struct.pack_into("<I", ambiguous, 0xC50, 7)
        add_v2_segment(ambiguous, 0x44000000, 0x200000, 0x20, 6)
        self.assertEqual(self.vpu_guest_map_base(
            page, page_after_publish=ambiguous), 0)
        oversized = bytearray(page)
        struct.pack_into("<I", oversized, 0xC50, 130)
        self.assertEqual(self.vpu_guest_map_base(
            page, page_after_publish=oversized), 0)

    def test_v2_shared_memory_address_rejects_missing_or_ambiguous_records(self):
        page = bytearray(CAPTURE.read_bytes())
        flags = struct.unpack_from("<Q", page, 0x10)[0]
        struct.pack_into("<Q", page, 0x10, flags & ~0x80)
        self.assertEqual(self.share_address(page), 0)
        self.assertEqual(self.share_size(page), 0)
        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<I", page, 0xC50, 7)
        add_v2_segment(page, 0x44000000, 0x200000, 0x20, 6)
        self.assertEqual(self.share_address(page), 0)
        self.assertEqual(self.share_size(page), 0)
        struct.pack_into("<Q", page, 0x28 + 5 * 24 + 16, 0)
        self.assertEqual(self.share_address(page), BAR2_BASE + 0x44000000)

    def test_v2_shared_memory_address_rejects_short_out_of_window_and_overflow(self):
        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<Q", page, 0x28 + 5 * 24 + 8, 0x1FFFFF)
        self.assertEqual(self.share_address(page), 0)
        self.assertEqual(self.share_address(CAPTURE.read_bytes(),
                                            bar2_size=0x431FFFFF), 0)
        self.assertEqual(self.share_address(CAPTURE.read_bytes(),
                                            bar2_base=(1 << 64) - 0x50000000,
                                            bar2_size=0x50000000), 0)
        self.assertEqual(self.share_address(CAPTURE.read_bytes(),
                                            bar2_base=(1 << 64) - 0x100000), 0)
        self.assertEqual(self.share_size(CAPTURE.read_bytes(),
                                         bar2_size=0x431FFFFF), 0)

    def test_v2_rejects_ambiguous_or_missing_firmware_segments(self):
        page = blank_page(2)
        struct.pack_into("<I", page, 0xC50, 2)
        add_v2_segment(page, FW_BASE, FW_BYTES, 4)
        add_v2_segment(page, FW_BASE + 0x1000000, FW_BYTES, 4, 1)
        self.assertEqual(self.decode(page), 0)
        struct.pack_into("<Q", page, 0x28 + 24 + 16, 1)
        self.assertEqual(self.decode(page), FW_BASE)
        struct.pack_into("<Q", page, 0x28 + 16, 1)
        self.assertEqual(self.decode(page), 0)

    def test_rejects_short_range_bad_magic_version_and_count(self):
        page = blank_page(2)
        struct.pack_into("<I", page, 0xC50, 1)
        add_v2_segment(page, FW_BASE, FW_BYTES - 1, 4)
        self.assertEqual(self.decode(page), 0)
        struct.pack_into("<Q", page, 0x28, (1 << 64) - 0x1000)
        self.assertEqual(self.decode(page), 0)
        struct.pack_into("<I", page, 0, 0)
        self.assertEqual(self.decode(page), 0)
        page = blank_page(9)
        self.assertEqual(self.decode(page), 0)
        page = blank_page(2)
        struct.pack_into("<I", page, 0xC50, 130)
        self.assertEqual(self.decode(page), 0)
        page = blank_page(2)
        struct.pack_into("<I", page, 0xC50, 1)
        add_v2_segment(page, (1 << 64) - 0x1000, 0x2000, 4)
        self.assertEqual(self.decode(page), 0)

    def test_captured_windows_v2_page_translates_pb_and_packed_segments(self):
        page = CAPTURE.read_bytes()
        # These are the BAR2-relative ranges reconstructed by FUN_140027ab4.
        cases = (
            (0, 0x36000000),
            (0x1FFFFF, 0x361FFFFF),
            (0x200000, 0x605000000),
            (0x51FFFFF, 0x605000000 + 0x4FFFFFF),
            (0x5200000, 0x13A000000),
            (0x3EFFFFFF, 0x13A000000 + 0x39DFFFFF),
            (0x3F000000, FW_BASE),
            (0x42FFFFFF, FW_BASE + 0x3FFFFFF),
            (0x43000000, 0),
        )
        for gdpa, expected in cases:
            with self.subTest(gdpa=hex(gdpa)):
                self.assertEqual(self.translate_v2(page, BAR2_BASE + gdpa), expected)

    def test_v2_translator_rejects_invalid_or_unmapped_ranges(self):
        page = CAPTURE.read_bytes()
        self.assertEqual(self.translate_v2(page, (1 << 64) - 1), 0)
        # FW segment addresses are outputs of BAR2 translation, not valid
        # inputs to this Windows-compatible BAR2-window converter.
        self.assertEqual(self.translate_v2(page, FW_BASE), 0)
        page = bytearray(page)
        struct.pack_into("<I", page, 0xC50, 130)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0x200000), 0)
        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<Q", page, 0x28 + 2 * 24, (1 << 64) - 0x1000)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0x200000), 0)
        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<Q", page, 0xC90, (1 << 64) - 0x1000)
        self.assertEqual(self.translate_v2(page, BAR2_BASE), 0)
        page = bytearray(CAPTURE.read_bytes())
        struct.pack_into("<I", page, 0, 0)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0x200000), 0)
        self.assertEqual(self.translate_v2(CAPTURE.read_bytes(), 0x200000), 0)
        self.assertEqual(self.translate_v2(CAPTURE.read_bytes(), BAR2_BASE + 0x200000,
                                           bar2_size=BAR2_SIZE - 0x1000), 0)

    def test_v2_system_segments_do_not_consume_the_bar2_mapping_cursor(self):
        page = blank_page(2)
        struct.pack_into("<Q", page, 0x20, 0x4000)
        struct.pack_into("<I", page, 0xC50, 4)
        add_v2_segment(page, 0, 0x1000, 0x8, 0)
        add_v2_segment(page, 0, 0x1000, 0x10, 1)
        add_v2_segment(page, 0x200000000, 0x1000, 0x2, 2)
        add_v2_segment(page, 0x300000000, 0x1000, 0x4, 3)

        self.assertEqual(self.translate_v2(page, BAR2_BASE, bar2_size=0x4000),
                         0x200000000)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0xFFF,
                                           bar2_size=0x4000),
                         0x200000FFF)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0x1000,
                                           bar2_size=0x4000),
                         0x300000000)
        self.assertEqual(self.translate_v2(page, BAR2_BASE + 0x2000,
                                           bar2_size=0x4000), 0)

    def test_dispatcher_preserves_v1_body_and_routes_relative_v2_addresses(self):
        self.assertEqual(self.dispatch_page(blank_page(1), 0x12345678),
                         0xABC0000012345678)
        page = CAPTURE.read_bytes()
        for offset, expected in ((0, 0x36000000), (0x200000, 0x605000000),
                                 (0x3F000000, FW_BASE), (0x43000000, 0)):
            self.assertEqual(self.dispatch_page(page, offset), expected)
        self.assertEqual(self.dispatch_page(page, BAR2_BASE + 0x3F000000), 0)
        self.assertEqual(self.dispatch_page(page, 0x3F000000,
                                           bar2_base=0x900000000), FW_BASE)
        self.assertEqual(self.dispatch_page(blank_page(9), 0x200000), 0)

    def test_dispatcher_ignores_the_unrelated_mmu_aperture_fields(self):
        page = CAPTURE.read_bytes()
        for size, base in ((0, PCI_BAR2_SIZE), (0x800000, 0x100000000),
                           (BAR2_SIZE, BAR2_BASE)):
            self.assertEqual(self.dispatch_page(page, 0x3F000000,
                                               mmu_size=size, mmu_card_base=base), FW_BASE)
        self.assertEqual(self.dispatch_page(page, 0x200000, bar2_base=0), 0)
        self.assertEqual(self.dispatch_page(page, 0x200000,
                                           bar2_base=(1 << 64)-4096), 0)

    def test_dispatcher_fails_closed_on_missing_pointer_links(self):
        self.assertEqual(self.dispatch(None, 0x200000), 0)


if __name__ == "__main__":
    unittest.main()
