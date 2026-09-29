#!/usr/bin/env python3
"""Validate memory pool decoding before using it for PCI memory allocations."""
import ctypes
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]


class Range(ctypes.Structure):
    _fields_ = [(key, ctypes.c_uint64) for key in ('bar_offset', 'gpu_pa', 'size')]


class Layout(ctypes.Structure):
    _fields_ = [('pool', Range * 3)] + [(key, ctypes.c_uint64) for key in
                                      ('actual_size', 'shared_offset', 'shared_size')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--info', type=Path, default=ROOT / 'reports/device-info.bin')
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/memory-layout-validation.json')
    args = parser.parse_args()
    out = ROOT / 'build/memory'
    out.mkdir(parents=True, exist_ok=True)
    so = out / 'layout.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/memory_layout_wrapper.c'),
                    '-o', str(so)], check=True)
    lib = ctypes.CDLL(str(so))
    lib.parse_memory.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
                                ctypes.POINTER(Layout)]
    lib.parse_memory.restype = ctypes.c_int
    source = args.info.read_bytes()
    oracle = ReferenceOracle()
    platform, info, config = 0x200000, 0x204000, 0x206000
    oracle.put64(platform + 0x1cb0, info)
    oracle.put64(platform + 0x10, config)
    oracle.put64(config + 0x28, 0x40000000)
    cases = 0
    for first_size in (0x2000000, 0x5000000, 0x6000000):
        for fw_size in (0x800000, 0x1000000, 0x4000000):
            raw = bytearray(source)
            actual = 0x200000 + first_size + 0x39e00000 + fw_size
            struct.pack_into('<Q', raw, 0x30, first_size)
            struct.pack_into('<Q', raw, 0x60, fw_size)
            struct.pack_into('<Q', raw, 0x20, actual)
            struct.pack_into('<Q', raw, 0xa0, actual)  # Shared segment starts after all mapped memory.
            oracle.uc.mem_write(info, bytes(raw))
            assert oracle.run(0x140026390, [platform], [(0x140026390, 0x1400265cb)]) == 0
            layout = Layout()
            assert lib.parse_memory(bytes(raw), len(raw), 0x400000000, ctypes.byref(layout)) == 0
            for i, offset in enumerate((0x490, 0x500, 0x4e8)):
                bar, size, gpu = struct.unpack('<3Q', oracle.uc.mem_read(platform + offset, 24))
                assert (layout.pool[i].bar_offset, layout.pool[i].gpu_pa, layout.pool[i].size) == (bar, gpu, size)
            cases += 1
    rejected = []
    mutations = [
        ('signature', 0, '<I', 0), ('version', 4, '<I', 1),
        ('count-overflow', 0xc50, '<I', 130), ('missing-pb', 0x10, '<Q', 0x3c1),
        ('missing-direct-fw', 0x10, '<Q', 0x351), ('pb-size', 0xc98, '<I', 4096),
        ('pb-address-overflow', 0xc90, '<Q', (1 << 64) - 4096),
        ('segment-unaligned', 0x28, '<Q', struct.unpack_from('<Q', source, 0x28)[0] + 1),
        ('segment-overlap', 0x40, '<Q', struct.unpack_from('<Q', source, 0x28)[0] + 4096),
        ('segment-size-overflow', 0x30, '<Q', (1 << 64) - 4096),
        ('small-first-segment', 0x30, '<Q', 4096),
        ('oversized-fw', 0x60, '<Q', 0x8000000),
        ('shared-overlap', 0xa0, '<Q', 0x40000000),
        ('shared-outside-bar', 0xa0, '<Q', 0x400000000),
        ('mapped-total-mismatch', 0x20, '<Q', 0x43001000),
    ]
    for name, offset, fmt, value in mutations:
        raw = bytearray(source)
        struct.pack_into(fmt, raw, offset, value)
        sentinel = bytes([0xa5]) * ctypes.sizeof(Layout)
        layout = Layout.from_buffer_copy(sentinel)
        assert lib.parse_memory(bytes(raw), len(raw), 0x400000000, ctypes.byref(layout)) < 0, name
        assert bytes(layout) == sentinel, name
        rejected.append(name)
    layout = Layout()
    assert lib.parse_memory(source[:128], 128, 0x400000000, ctypes.byref(layout)) < 0
    assert lib.parse_memory(source, len(source), 0x100000, ctypes.byref(layout)) < 0
    assert lib.parse_memory(source, len(source), 0x400000000, ctypes.byref(layout)) == 0
    report = {'reference_sha256': oracle.pe.sha256, 'device_info_sha256': hashlib.sha256(source).hexdigest(),
              'oracle_cases_passed': cases, 'rejected_mutations': rejected,
              'truncated_input_rejected': True, 'short_bar_rejected': True,
              'hardware_written': False,
              'pools': [{key: getattr(r, key) for key, _ in Range._fields_} for r in layout.pool]}
    report['device_info_path'] = str(args.info.resolve())
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
