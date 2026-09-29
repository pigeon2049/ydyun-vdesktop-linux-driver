#!/usr/bin/env python3
"""Compare post-connect context descriptors with original Windows instructions.

MMIO is recorded only. Physical-address helper results are test inputs; the
actual descriptor construction, caching and publication wrappers execute.
"""
import ctypes
import json
from pathlib import Path
import random
import struct
import subprocess

from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
CONFIG, DEVICE, GPU, LAYOUT = 0x200000, 0x210000, 0x214000, 0x220000
CONTEXT, ALLOC, CPU_IMAGE, MMU, ROOT_DESC, ROOT_PHYS = range(0x224000, 0x22a000, 0x1000)
INFO, GPU_CONFIG = 0x22a000, 0x22b000


class Addresses(ctypes.Structure):
    _fields_ = [(x, ctypes.c_uint64) for x in ('root_pa', 'firmware_gpa', 'firmware_va',
                                             'info_gpa', 'aperture_gpa')]
    _fields_ += [('device_config', ctypes.c_uint32)]


def main():
    out = ROOT / 'build/firmware'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'context.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-fsanitize=undefined', '-shared', '-fPIC',
                    str(ROOT / 'tests/fw_context_wrapper.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.build.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Addresses)]
    lib.refresh.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint64]
    o = ReferenceOracle()
    o.uc.mem_write(CONFIG, b'\1')
    for address, value in ((DEVICE, CONFIG), (DEVICE + 0x418, GPU), (DEVICE + 0x420, LAYOUT),
                           (CONTEXT, ALLOC), (ALLOC, CPU_IMAGE), (CONTEXT + 0x38, MMU),
                           (MMU + 0x20, ROOT_DESC), (ROOT_DESC + 0x10, ROOT_PHYS),
                           (GPU + 0x1cb8, INFO), (GPU + 0x950, GPU_CONFIG)):
        o.put64(address, value)
    writes = []
    inputs = Addresses()
    descriptor_gpa = 0x12345000

    def physical():
        cpu = o.arg(0)
        known = {CPU_IMAGE: inputs.firmware_gpa, INFO: inputs.info_gpa,
                 GPU + 0x408: inputs.aperture_gpa}
        if cpu not in known:
            assert cpu == o.get64(DEVICE + 0x20)
        o.ret(known.get(cpu, descriptor_gpa))

    def publish():
        assert o.arg(0) == GPU + 8
        writes.append((o.arg(1), o.arg(2)))
        o.ret()

    o.put64(0x1401381b8, 0x2f8040)
    o.hooks[0x2f8040] = physical
    o.hooks[0x140027780] = publish
    ranges = [(0x14001ecdc, 0x14001edef), (0x140018b20, 0x140018b38),
              (0x1400229c0, 0x1400229d2), (0x1400231ec, 0x140023251),
              (0x1400083b0, 0x140008402), (0x140130c10, 0x140130c12)]
    candidate = ctypes.create_string_buffer(96)
    rng = random.Random(275)
    cases = []
    for n in range(33):
        inputs = (Addresses(0x605800000, 0x83f000000, 0xe1c0000000,
                            0x123000, 0x125408, 0) if n == 0 else
                  Addresses(rng.randrange(1, 1 << 28) << 12,
                            rng.randrange(1, 1 << 36) << 12,
                            rng.randrange((1 << 40) - 0x800000) & ~4095,
                            rng.randrange(1, 1 << 40) << 3,
                            rng.randrange(1, 1 << 40) << 3, rng.getrandbits(32)))
        o.put64(DEVICE + 0x20, 0)
        o.put64(ROOT_PHYS, inputs.root_pa)
        o.put32(LAYOUT + 0x3c, 0x6ddd0)
        o.put64(LAYOUT + 0x58, inputs.firmware_va)
        o.put64(LAYOUT + 0x30, inputs.firmware_va + 0x6ddd0)
        o.put32(GPU_CONFIG + 0x18, inputs.device_config)
        writes.clear()
        o.run(0x14001ecdc, [DEVICE, CONTEXT], ranges)
        desc = o.get64(DEVICE + 0x20)
        expected = bytes(o.uc.mem_read(desc, 80))
        ctypes.memset(candidate, 0xa5, 96)
        assert lib.build(candidate, 80, ctypes.byref(inputs)) == 0
        assert candidate.raw == expected + b'\xa5' * 16
        assert writes == [(0xf0, descriptor_gpa)]
        if n == 0:
            (out / 'context-example.bin').write_bytes(expected)
        # Mimic Host-owned changes and different source fields on re-publish.
        # Reference must retain the cached root/config, changing only two GPAs.
        cached = bytearray(rng.randbytes(80))
        o.uc.mem_write(desc, bytes(cached))
        ctypes.memmove(candidate, bytes(cached), 80)
        o.put64(ROOT_PHYS, inputs.root_pa ^ 0x1000)
        o.put32(GPU_CONFIG + 0x18, inputs.device_config ^ 0xffffffff)
        inputs.info_gpa += 8
        inputs.aperture_gpa += 8
        old_cursor = o.cursor
        writes.clear()
        o.run(0x14001ecdc, [DEVICE, CONTEXT], ranges)
        assert o.cursor == old_cursor and o.get64(DEVICE + 0x20) == desc
        assert lib.refresh(candidate, 80, inputs.info_gpa, inputs.aperture_gpa) == 0
        assert candidate.raw == bytes(o.uc.mem_read(desc, 80)) + b'\xa5' * 16
        assert writes == [(0xf0, descriptor_gpa)]
        cases.append({'new_and_cached_match': True, 'case': n})
    invalid = [('root_pa', 0), ('root_pa', 1), ('root_pa', 1 << 40),
               ('firmware_gpa', 1), ('firmware_gpa', (1 << 64) - 4096),
               ('firmware_va', 1), ('firmware_va', (1 << 40) - 4096),
               ('info_gpa', 0), ('info_gpa', 1), ('aperture_gpa', 0), ('aperture_gpa', 1)]
    before = candidate.raw
    for field, value in invalid:
        bad = Addresses.from_buffer_copy(inputs)
        setattr(bad, field, value)
        assert lib.build(candidate, 80, ctypes.byref(bad)) < 0 and candidate.raw == before
    assert lib.build(candidate, 79, ctypes.byref(inputs)) < 0 and candidate.raw == before
    assert lib.build(candidate, 80, None) < 0 and candidate.raw == before
    for size, info, aperture in ((79, 8, 8), (80, 0, 8), (80, 1, 8), (80, 8, 0), (80, 8, 1)):
        assert lib.refresh(candidate, size, info, aperture) < 0 and candidate.raw == before
    report = {'reference_sha256': o.pe.sha256, 'hardware_written': False,
              'new_descriptor_cases': len(cases), 'cached_descriptor_cases': len(cases),
              'bytes_compared_per_case': 80, 'invalid_cases': len(invalid) + 7,
              'publication_offset': '0xf0', 'guard_bytes_preserved': True,
              'example_system_memory_gpas_are_synthetic': True,
              'host_runtime_integration': False, 'hardware_acceleration_verified': False,
              'modeled_helpers': ['CPU allocation/memset', 'CPU physical address translation', 'MMIO recording'],
              'limits': ['Caller must supply verified persistent CPU buffers and device_config',
                         'Current firmware MMU mode only; no Native or alternate root path',
                         'Descriptor publication is after successful firmware connection',
                         'Runtime lifetime, Host mapping, and actual publication not tested']}
    (ROOT / 'reports/firmware-context-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
