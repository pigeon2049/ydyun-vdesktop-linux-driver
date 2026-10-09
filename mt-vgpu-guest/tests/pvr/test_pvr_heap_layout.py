"""Validate PVR heap ownership against the Windows instruction oracle."""
import ctypes
import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = get_repo_root()
sys.path.insert(0, str(ROOT / 'scripts'))
from reference_oracle import ReferenceOracle


class Range(ctypes.Structure):
    _fields_ = [(k, ctypes.c_uint64) for k in ('cpu', 'device', 'size')]


class Plan(ctypes.Structure):
    _fields_ = [('range', Range * 4), ('mmu_host_base', ctypes.c_uint64)]


class HeapLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = Path(cls.temp.name)
        source = path / 'wrapper.c'
        source.write_text('#include "mt_pvr_heap_layout.h"\n'
            'int parse(const void *p, u32 n, u64 b, u64 s, struct mt_pvr_heap_layout *o) '
            '{ return mt_pvr_heap_parse(p,n,b,s,o); }\n')
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
            '-I', str(ROOT / 'kernel'), str(source), '-o', str(path / 'plan.so')], check=True)
        cls.lib = ctypes.CDLL(str(path / 'plan.so'))
        cls.lib.parse.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
                                  ctypes.c_uint64, ctypes.POINTER(Plan)]
        cls.raw = (ROOT / 'reports/r18b-linux-v2-info-full.bin').read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_windows_pool_ownership(self):
        oracle = ReferenceOracle()
        platform, info, config = 0x200000, 0x204000, 0x206000
        oracle.put64(platform + 0x1cb0, info)
        oracle.put64(platform + 0x10, config)
        oracle.put64(config + 0x28, 0x40000000)
        oracle.uc.mem_write(info, self.raw)
        self.assertEqual(oracle.run(0x140026390, [platform], [(0x140026390, 0x1400265cb)]), 0)
        plan = Plan()
        self.assertEqual(self.lib.parse(self.raw, len(self.raw), 0x800000000,
                                       0x400000000, ctypes.byref(plan)), 0)
        normal = struct.unpack('<3Q', oracle.uc.mem_read(platform + 0x490, 24))
        general = struct.unpack('<3Q', oracle.uc.mem_read(platform + 0x4c0, 24))
        firmware = struct.unpack('<3Q', oracle.uc.mem_read(platform + 0x4e8, 24))
        self.assertEqual((plan.range[1].device, plan.range[1].size, plan.mmu_host_base), normal)
        self.assertEqual((plan.range[3].device, plan.range[3].size), general[:2])
        self.assertEqual(plan.range[0].device, firmware[0])
        self.assertEqual(plan.range[0].size + plan.range[2].size, firmware[1])
        for i, r in enumerate(plan.range):
            self.assertEqual(r.cpu, 0x800000000 + r.device)
            for other in plan.range[:i]:
                self.assertTrue(r.device + r.size <= other.device or other.device + other.size <= r.device)

    def test_failure_leaves_output_unchanged(self):
        cases = [(self.raw[:128], 0x800000000, 0x400000000),
                 (self.raw, 0, 0x400000000), (self.raw, 0x800000001, 0x400000000),
                 (self.raw, 0xfffffffffffff000, 0x400000000),
                 (self.raw, 0x800000000, 0x100000)]
        small = bytearray(self.raw)
        struct.pack_into('<Q', small, 0x60, 0x800000)
        struct.pack_into('<Q', small, 0x20, 0x3f800000)
        struct.pack_into('<Q', small, 0xa0, 0x3f800000)
        cases.append((bytes(small), 0x800000000, 0x400000000))
        for raw, base, size in cases:
            sentinel = b'\xa5' * ctypes.sizeof(Plan)
            plan = Plan.from_buffer_copy(sentinel)
            self.assertLess(self.lib.parse(raw, len(raw), base, size, ctypes.byref(plan)), 0)
            self.assertEqual(bytes(plan), sentinel)


if __name__ == '__main__':
    unittest.main()
