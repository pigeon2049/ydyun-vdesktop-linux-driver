#!/usr/bin/env python3
"""Compare complete sparse page-table construction against original MMU code.

GPU allocation and CPU->GPU physical-address translation are modeled. The
reference allocation/reservation/walk/flag conversion/PTE instructions execute.
No page table is uploaded and no context root is published to hardware.
"""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
MMU, CONTEXT, GPU = 0x210000, 0x212000, 0x214000
TABLE_PA = 0x605800000
CAPACITY = 64 * 4096


class Range(ctypes.Structure):
    _fields_ = [('va', ctypes.c_uint64), ('pa', ctypes.c_uint64),
                ('size', ctypes.c_uint32), ('flags', ctypes.c_uint32)]


class MMUOracle(ReferenceOracle):
    def __init__(self, table_pa=TABLE_PA):
        super().__init__()
        self.table_pa = table_pa
        self.allocations = []
        self.allocated = 0
        self.put64(0x1401380d0, 0x2f8030)
        self.hooks[0x2f8030] = lambda: self.ret(self.allocate(self.arg(1)))
        self.hooks[0x140022218] = self.allocate_gpu
        # Mapping source is an opaque CPU address. Supply the chosen GPU PA
        # through both physical-address helpers without dereferencing it.
        self.put64(0x1401381b8, 0x2f8040)  # MmGetPhysicalAddress.
        self.hooks[0x2f8040] = lambda: self.ret(self.arg(0))
        self.hooks[0x140021d90] = lambda: self.ret(self.arg(1))
        assert self.run(0x14002a34c, [MMU, GPU], [(0x14002a0d0, 0x14002a4ec)]) == 0
        self.descriptors = self.get64(MMU + 8)
        self.put64(CONTEXT + 8, GPU)
        self.put64(CONTEXT + 0x10, MMU)
        self.put64(CONTEXT + 0x18, MMU + 0x18)
        self.put32(CONTEXT + 0x40, 0)
        assert self.run(0x1400180ec, [GPU, self.descriptors, CONTEXT + 0x20],
                        [(0x1400180ec, 0x1400181dd)]) == 0
        assert self.allocated == 4096

    def allocate_gpu(self):
        size = self.arg(1) & 0xffffffff
        assert size in (4096, 0x3000)
        data = self.allocate(size)
        descriptor = self.allocate(0x40)
        physical = self.allocate(size // 4096 * 8)
        pa = self.table_pa + self.allocated
        self.put64(descriptor, data)
        self.put64(descriptor + 0x10, physical)
        for i in range(size // 4096):
            self.put64(physical + i * 8, pa + i * 4096)
        self.allocations.append((data, size, pa))
        self.allocated += size
        self.ret(descriptor)

    def map_range(self, item):
        return self.run(0x140018f68, [CONTEXT, item.va, item.pa, item.size, 0, item.flags], [
            (0x1400180ec, 0x1400183f3), (0x140018bd0, 0x140018e0b),
            (0x140018f68, 0x140019549), (0x1400197b4, 0x140019986),
            (0x14002429c, 0x1400242c2), (0x14002a2dc, 0x14002a34c),
            (0x14002a5f8, 0x14002a600), (0x14002a810, 0x14002a913),
            (0x140130c10, 0x140130c12)], count=5000000) & 255

    def image(self):
        return b''.join(bytes(self.uc.mem_read(address, size)) for address, size, _ in self.allocations)


def main():
    out = ROOT / 'build/mmu'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'bootstrap.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/mmu_bootstrap_wrapper.c'),
                    '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.build.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
                          ctypes.POINTER(Range), ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    lib.dummy.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64]
    candidate = ctypes.create_string_buffer(CAPACITY)
    used = ctypes.c_uint32()
    rng = random.Random(275)
    scenarios = [
        [Range(0xe1c0000000, 0x771fef000, 0x800000, 0x1c)],
        [Range(0x3ffff000, 0x600000000, 0x4000, 0x1c)],  # Cross PC boundary.
        [Range(0x1ff000, 0x600000000, 0x3000, 0)],       # Cross PT boundary.
        [Range((1 << 40) - 4096, 0xffffffe000, 4096, 0x1f)],
        [Range(0x1000, 0x600000000, 4096, 1), Range(0x9000, 0x600002000, 8192, 2)],
        [Range(0xe1c0000000, 0x771fef000, 0x800000, 0x1c),
         Range(0xa000e00000, 0x36000000, 0x200000, 0x1c),
         Range(0x81ffc00000, 0x605900000, 0x100000, 0x1c),
         Range(0x81ffd00000, 0x605a00000, 0x1000, 0x1c),
         Range(0x81ffd01000, 0x605a01000, 0x2000, 0x1c),
         Range(0x84ffe00000, 0x605a10000, 0x80000, 0x1c),
         Range(0x84ffe80000, 0x605a90000, 0x80000, 0x1c)],
    ]
    for flags in range(32):
        scenarios.append([Range((rng.randrange(1, 1 << 26) << 12),
                                 0x700000000, rng.randrange(1, 8) * 4096, flags)])
    pages_compared = 0
    for number, scenario in enumerate(scenarios):
        oracle = MMUOracle()
        for item in scenario:
            assert oracle.map_range(item) == 1
        expected = oracle.image()
        items = (Range * len(scenario))(*scenario)
        ctypes.memset(candidate, 0xa5, CAPACITY)
        assert lib.build(candidate, CAPACITY, TABLE_PA, items, len(items), ctypes.byref(used)) == 0
        assert used.value * 4096 == len(expected)
        assert candidate.raw[:len(expected)] == expected, f'Page tree differs: scenario {number}'
        assert candidate.raw[len(expected):] == b'\xa5' * (CAPACITY - len(expected))
        pages_compared += used.value
        if number == 5:
            (out / 'bootstrap-page-tables.bin').write_bytes(expected)
            bootstrap = {'table_pa': hex(TABLE_PA), 'pages': used.value,
                         'purpose': 'Synthetic combined-tree exercise, not the firmware-context layout',
                         'sha256': hashlib.sha256(expected).hexdigest(),
                         'ranges': [{'va': hex(r.va), 'pa': hex(r.pa), 'bytes': r.size,
                                     'map_flags': hex(r.flags)} for r in scenario],
                         'device_ranges_reserved': False, 'uploaded': False,
                         'paging_command_system_memory_included': False}

    # Default PD/PT/zero-page construction is a different reference path.
    dummy_cases = 0
    for pa in (0x605840000, 0xfffffff000 - 0x2000, 0x1000):
        oracle = MMUOracle(pa - 4096)
        assert oracle.run(0x14002a4ec, [MMU], [(0x14002a4ec, 0x14002a5f5),
                                               (0x14002a8d8, 0x14002a913)]) == 0
        expected = oracle.image()[4096:]
        ctypes.memset(candidate, 0xa5, CAPACITY)
        assert lib.dummy(candidate, CAPACITY, pa) == 0
        assert candidate.raw[:0x3000] == expected
        assert candidate.raw[0x3000:] == b'\xa5' * (CAPACITY - 0x3000)
        dummy_cases += 1

    invalid = [
        ([Range(1, 0x700000000, 4096, 0)], TABLE_PA, CAPACITY),
        ([Range(4096, 0x700000001, 4096, 0)], TABLE_PA, CAPACITY),
        ([Range(4096, 0x700000000, 4097, 0)], TABLE_PA, CAPACITY),
        ([Range(1 << 40, 0x700000000, 4096, 0)], TABLE_PA, CAPACITY),
        ([Range((1 << 40) - 4096, 0x700000000, 8192, 0)], TABLE_PA, CAPACITY),
        ([Range(0, (1 << 40) - 4096, 8192, 0)], TABLE_PA, CAPACITY),
        ([Range(0, 0x700000000, 4096, 0x20)], TABLE_PA, CAPACITY),
        ([Range(0, 0x700000000, 4096, 0)] * 2, TABLE_PA, CAPACITY),
        ([Range(0, TABLE_PA, 4096, 0)], TABLE_PA, CAPACITY),
        ([Range(0, 0x700000000, 4096, 0)], TABLE_PA + 1, CAPACITY),
        ([Range(0, 0x700000000, 4096, 0)], (1 << 40) - 4096, CAPACITY),
        ([Range(0, 0x700000000, 4096, 0)], TABLE_PA, 8192),
        ([Range(0, 0x700000000, 0x4001000, 0)], TABLE_PA, CAPACITY),
        ([Range((i + 1) * 0x80000000 - 4096, 0x700000000 + i * 0x400000,
                0x400000, 0) for i in range(16)], TABLE_PA, CAPACITY),
    ]
    for items, pa, size in invalid:
        ranges = (Range * len(items))(*items)
        ctypes.memset(candidate, 0xa5, CAPACITY)
        used.value = 0x12345678
        assert lib.build(candidate, size, pa, ranges, len(ranges), ctypes.byref(used)) < 0
        assert candidate.raw == b'\xa5' * CAPACITY and used.value == 0x12345678
    report = {'reference_sha256': oracle.pe.sha256, 'hardware_written': False,
              'sparse_tree_cases_passed': len(scenarios), 'table_pages_compared': pages_compared,
              'dummy_cases_passed': dummy_cases, 'invalid_inputs_rejected': len(invalid),
              'multi_range_test_fixture': bootstrap,
              'modeled_helpers': ['CPU allocation/memset', 'GPU allocation descriptors and addresses',
                                  'CPU-to-GPU physical-address translation'],
              'gpu_mappings_installed': False, 'firmware_connection_verified': False}
    (ROOT / 'reports/mmu-bootstrap-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
