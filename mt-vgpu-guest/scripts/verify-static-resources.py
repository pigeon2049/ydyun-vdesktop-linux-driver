#!/usr/bin/env python3
"""Execute original static-resource selection/allocation/writes in isolation."""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
PLAN, OUTPUT = 0x210000, 0x211000
SIZE = 0x80000


def main():
    out = ROOT / 'build/firmware'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'static.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/static_resources_wrapper.c'),
                    '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.initialize.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
    buffers = [ctypes.create_string_buffer(SIZE), ctypes.create_string_buffer(SIZE)]
    rng = random.Random(275)
    for case in range(2):
        oracle = ReferenceOracle()
        allocations = []
        baseline = bytes(SIZE) if case == 0 else rng.randbytes(SIZE)

        def allocate_resource():
            size = oracle.arg(1)
            assert size == (0x100000 if len(allocations) < 2 else SIZE)
            assert (oracle.arg(2), oracle.arg(3)) == (0x53585443, 1)
            descriptor, data = oracle.allocate(0x40), oracle.allocate(size)
            oracle.uc.mem_write(data, (baseline * 2)[:size])
            oracle.put64(descriptor, data)
            allocations.append((descriptor, data, size))
            oracle.ret(descriptor)

        oracle.hooks[0x140022218] = allocate_resource
        for i, offset in enumerate((0x80, 0x120, 0x140, 0x160)):
            oracle.put32(PLAN + offset + 8, 0x100000 if i < 2 else SIZE)
            oracle.put32(PLAN + offset + 0x1c, i + 123)
        assert oracle.run(0x140019cc4, [PLAN, 0, OUTPUT], [
            (0x140019c5c, 0x140019f0d), (0x14001e1e4, 0x14001e23d)]) == 0
        assert len(allocations) == 4
        for i, (descriptor, data, size) in enumerate(allocations):
            assert oracle.get64(OUTPUT + i * 0x40 + 0x30) == descriptor
            assert oracle.get64(OUTPUT + i * 0x40 + 8) == size
            if i < 2:
                assert bytes(oracle.uc.mem_read(data, size)) == baseline * 2
        for buffer in buffers:
            ctypes.memmove(buffer, baseline, SIZE)
        assert lib.initialize(buffers[0], SIZE, buffers[1], SIZE) == 0
        for buffer, (_, data, size) in zip(buffers, allocations[2:]):
            assert buffer.raw == bytes(oracle.uc.mem_read(data, size))
        if case == 0:
            hashes = {}
            for name, buffer in zip(('yuv', 'dm-kill'), buffers):
                (out / f'{name}-initial.bin').write_bytes(buffer.raw)
                hashes[name] = hashlib.sha256(buffer.raw).hexdigest()
    before = [b.raw for b in buffers]
    for sizes in ((SIZE - 1, SIZE), (SIZE, SIZE - 1)):
        assert lib.initialize(buffers[0], sizes[0], buffers[1], sizes[1]) < 0
        assert [b.raw for b in buffers] == before
    report = {'reference_sha256': oracle.pe.sha256, 'hardware_written': False,
              'reference_function': '0x140019cc4', 'full_resource_cases_passed': 2,
              'bytes_compared_per_case': SIZE * 4 + SIZE * 2,
              'preserves_unspecified_bytes': True, 'invalid_sizes_rejected': 2,
              'initial_images_sha256': hashes,
              'modeled_helpers': ['CPU memset', 'GPU allocation returning modeled CPU mappings'],
              'gpu_program_executed': False}
    (ROOT / 'reports/static-resources-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
