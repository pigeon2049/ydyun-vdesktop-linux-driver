#!/usr/bin/env python3
"""Compare the Guest platform-window refresh with original instructions."""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
PLATFORM, INFO, OS_INFO = 0x200000, 0x204000, 0x206000
SIZE = 0x118


class Inputs(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in
                ('system_memory_bytes', 'platform_18', 'platform_9d8', 'platform_9e0')]


def main():
    library = ROOT / 'build/firmware/guest-windows.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-fsanitize=undefined', '-shared', '-fPIC',
                    str(ROOT / 'tests/guest_windows_wrapper.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.refresh.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
                           ctypes.c_uint32, ctypes.POINTER(Inputs)]
    raw = (ROOT / 'reports/device-info.bin').read_bytes()
    o = ReferenceOracle()
    rng = random.Random(26390)
    candidate = ctypes.create_string_buffer(SIZE + 16)
    cases = []
    memory_kib = int(next(line.split()[1] for line in Path('/proc/meminfo').read_text().splitlines()
                          if line.startswith('MemTotal:')))
    defaults = struct.unpack('<QQ', o.pe.read(0x141106b08, 16))
    assert defaults == (0x8000000000, 0)
    for n in range(65):
        info = bytearray(raw)
        if n == 0:
            a = Inputs(memory_kib * 1024, 0, *defaults)
            seed = bytearray(SIZE)
            # Initial BAR2 descriptor comes from resource enumeration.
            struct.pack_into('<QQQ', seed, 0, 0x800000000, 0x400000000, 0)
        else:
            bits = (n - 1) % 8
            flags = (bits & 1) | ((bits & 2) << 3) | ((bits & 4) << 5)
            struct.pack_into('<Q', info, 0x10, flags)
            struct.pack_into('<Q', info, 0x20, 0x80000000 + rng.randrange(256) * 4096)
            struct.pack_into('<Q', info, 0xc90, rng.randrange(1, 1 << 20) << 12)
            struct.pack_into('<I', info, 0xc98, rng.randrange(1, 1024) << 12)
            a = Inputs(rng.randrange(0x20000000, 1 << 44),
                       rng.getrandbits(64), rng.getrandbits(64), rng.getrandbits(64))
            seed = bytearray(rng.randbytes(SIZE))
        o.uc.mem_write(PLATFORM, bytes(0x2000))
        o.put64(PLATFORM + 0x10, OS_INFO)
        o.put64(OS_INFO + 0x28, a.system_memory_bytes)
        o.put64(PLATFORM + 0x18, a.platform_18)
        o.put64(PLATFORM + 0x9d8, a.platform_9d8)
        o.put64(PLATFORM + 0x9e0, a.platform_9e0)
        o.put64(PLATFORM + 0x1cb0, INFO)
        o.uc.mem_write(INFO, bytes(info))
        o.uc.mem_write(PLATFORM + 0x400, bytes(seed))
        assert o.run(0x140026390, [PLATFORM], [(0x140026390, 0x1400265cb)]) == 0
        expected = bytes(o.uc.mem_read(PLATFORM + 0x400, SIZE))
        ctypes.memmove(candidate, bytes(seed) + b'\xa5' * 16, SIZE + 16)
        buf = ctypes.create_string_buffer(bytes(info))
        assert lib.refresh(candidate, SIZE, buf, len(info), ctypes.byref(a)) == 0
        assert candidate.raw == expected + b'\xa5' * 16, n
        cases.append(dict(flags=hex(struct.unpack_from('<Q', info, 0x10)[0]),
                          system_memory_bytes=a.system_memory_bytes,
                          compared_bytes=SIZE, preserved_unwritten_fields=True))
        if n == 0:
            (ROOT / 'build/firmware/guest-windows-example.bin').write_bytes(expected)
            example = dict(inputs={k: getattr(a, k) for k, _ in a._fields_},
                           sha256=hashlib.sha256(expected).hexdigest(),
                           fields={hex(0x400 + i): hex(struct.unpack_from('<Q', expected, i)[0])
                                   for i in range(0, SIZE, 8)})
            # Cross-check the exact first-trial allocations against this
            # independently executed initialization path.
            assert struct.unpack_from('<QQQ', expected, 0x90) == (0xa00000, 0x1800000, 0x605800000)
            assert struct.unpack_from('<QQQ', expected, 0xe8) == (0x3f000000, 0x4000000, 0x771fef000)

    invalid = []
    def reject(name, info=raw, size=SIZE, length=None, inputs=None, null_out=False, null_info=False, null_inputs=False):
        ctypes.memset(candidate, 0xa5, SIZE + 16)
        original = candidate.raw
        buf = ctypes.create_string_buffer(bytes(info))
        inputs = inputs or Inputs(1 << 33, 0, *defaults)
        ret = lib.refresh(None if null_out else candidate, size, None if null_info else buf,
                          len(info) if length is None else length,
                          None if null_inputs else ctypes.byref(inputs))
        assert ret < 0 and candidate.raw == original, (name, ret)
        invalid.append(dict(case=name, errno=ret, output_unchanged=True))

    reject('short output', size=SIZE - 1)
    reject('short info', length=0xcc7)
    reject('null output', null_out=True)
    reject('null info', null_info=True)
    reject('null inputs', null_inputs=True)
    reject('system memory underflow', inputs=Inputs(0x1fffffff, 0, *defaults))
    for name, offset, value, fmt in (
        ('bad magic', 0, 0, '<I'), ('bad version', 4, 3, '<I'),
        ('empty segments', 0xc50, 0, '<I'), ('too many segments', 0xc50, 130, '<I'),
        ('missing firmware', 0x68, 0, '<Q'),
        ('actual size underflow', 0x20, 1, '<Q'),
        ('normal address overflow', 0x28, (1 << 64) - 1, '<Q'),
    ):
        data = bytearray(raw)
        struct.pack_into(fmt, data, offset, value)
        reject(name, info=data)
    # The first firmware segment is index 2 at +0x58, flags at +0x68.
    data = bytearray(raw)
    struct.pack_into('<Q', data, 0x10, 0x11)
    struct.pack_into('<Q', data, 0x60, 0x100000)
    reject('legacy firmware reserve underflow', info=data)
    struct.pack_into('<Q', data, 0x60, 0x4000000)
    struct.pack_into('<Q', data, 0x58, (1 << 64) - 1)
    reject('legacy firmware address overflow', info=data)
    report = dict(reference_sha256=o.pe.sha256, entry='140026390', passed=True,
                  valid_cases=cases, rejected_cases=invalid, current_info_example=example,
                  hardware_written=False, runtime_publication_implemented=False,
                  scope='Refresh of recovered platform region only. Initial resource fields and lifetime are separate; firmware connection remains unverified.')
    (ROOT / 'reports/guest-windows-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} original-instruction cases, {len(invalid)} rejected inputs; no device access')


if __name__ == '__main__':
    main()
