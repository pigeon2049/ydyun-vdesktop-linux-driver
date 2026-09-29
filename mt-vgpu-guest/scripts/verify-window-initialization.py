#!/usr/bin/env python3
"""Original platform zeroing, resource enumeration and Guest window refresh."""
import ctypes
import json
from pathlib import Path
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
PCI, OS_INFO, RESOURCES, INFO, ARGS, OUT = [0x200000 + i * 4096 for i in range(6)]
SIZE = 0x118


class Inputs(ctypes.Structure):
    _fields_ = [(x, ctypes.c_uint64) for x in
                ('system_memory_bytes', 'platform_18', 'platform_9d8', 'platform_9e0')]


def main():
    library = ROOT / 'build/firmware/guest-windows.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-fsanitize=undefined', '-shared', '-fPIC',
                    str(ROOT / 'tests/guest_windows_wrapper.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.build.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
                         ctypes.c_uint32, ctypes.POINTER(Inputs), ctypes.c_uint64, ctypes.c_uint64]
    o = ReferenceOracle()
    raw = (ROOT / 'reports/device-info.bin').read_bytes()
    source = ctypes.create_string_buffer(raw)
    candidate = ctypes.create_string_buffer(SIZE + 16)
    o.uc.mem_write(PCI, struct.pack('<HH', 0x1ed5, 0x0222))
    o.put64(ARGS, PCI)
    o.put64(ARGS + 8, OS_INFO)
    o.uc.mem_write(ARGS + 16, b'\1')
    allocations = []

    def allocate_dirty():
        assert (o.arg(0), o.arg(1) & 255, o.arg(2)) == (0x3e48, 1, 0x504c5469)
        p = o.allocate(0x3e48)
        o.uc.mem_write(p, b'\xa5' * 0x3e48)
        allocations.append(p)
        o.ret(p)

    def inspect_initializer():
        assert o.arg(0) == allocations[-1] + 8
        assert bytes(o.uc.mem_read(allocations[-1], 0x3e48)) == bytes(0x3e48)
        o.ret()

    o.hooks[0x140007d9c] = allocate_dirty
    o.hooks[0x140025b88] = inspect_initializer
    assert o.run(0x1400222b4, [OUT, ARGS, OUT + 8], [(0x1400222b4, 0x14002242f)]) == 0
    assert o.get64(OUT) == allocations[-1]
    platform = allocations[-1] + 8
    o.put64(platform + 8, PCI)
    o.put64(platform + 0x10, OS_INFO)
    o.put64(OS_INFO + 0x20, RESOURCES)
    o.put64(platform + 0x1cb0, INFO)
    o.uc.mem_write(platform + 0x44, b'\1')
    o.uc.mem_write(INFO, raw)
    for offset, value in ((0x184, 2), (0x188, 0), (0x18c, 4), (0x2f8, 1)):
        o.put32(platform + offset, value)
    a = Inputs(16 << 30, 0, 0x8000000000, 0)
    o.put64(OS_INFO + 0x28, a.system_memory_bytes)
    o.put64(platform + 0x18, a.platform_18)
    o.put64(platform + 0x9d8, a.platform_9d8)
    o.put64(platform + 0x9e0, a.platform_9e0)
    cases = []
    for bar_base in (0x800000000, 0x1000000000, 0x2000000000):
        for bar_size in (0x80000000, 0x400000000):
            o.uc.mem_write(platform + 0x400, bytes(SIZE))
            for offset, value in ((0x10, 0xfc610000), (0x14, 0xfc620000),
                                  (0x18, (bar_base & 0xffffffff) | 0xc), (0x1c, bar_base >> 32)):
                o.put32(PCI + offset, value)
            o.put32(RESOURCES + 0x10, 3)
            flags = 0x280 if bar_size > 0xffffffff else 0x80
            encoded_size = bar_size >> 8 if flags == 0x280 else bar_size
            for i, (base, size, kind, bits) in enumerate((
                    (0xfc610000, 0x10000, 3, 0x80), (0xfc620000, 0x10000, 3, 0x80),
                    (bar_base, encoded_size, 7 if flags == 0x280 else 3, flags))):
                o.uc.mem_write(RESOURCES + 0x14 + i * 20,
                               struct.pack('<BBHQI', kind, 0, bits, base, size) + bytes(4))
            o.run(0x14002fe44, [platform], [(0x14002fe44, 0x140030151)])
            expected_seed = struct.pack('<QQQ', bar_base, bar_size, 0) + bytes(SIZE - 24)
            assert bytes(o.uc.mem_read(platform + 0x400, SIZE)) == expected_seed
            assert o.run(0x140026390, [platform], [(0x140026390, 0x1400265cb)]) == 0
            window = bytes(o.uc.mem_read(platform + 0x400, SIZE))
            # The subsequent Guest segment builder writes a separate WDDM
            # output; confirm it does not fill the Native-only window slots.
            assert o.run(0x1411ce6b8, [platform, 0x20c000], [
                (0x1411ce6b8, 0x1411ce7c9), (0x14002c944, 0x14002cc75),
                (0x140027ab4, 0x140027bec)]) == 0
            assert bytes(o.uc.mem_read(platform + 0x400, SIZE)) == window
            segment_count = struct.unpack('<I', o.uc.mem_read(0x20c000, 4))[0]
            assert segment_count > 0
            ctypes.memset(candidate, 0xa5, SIZE + 16)
            assert lib.build(candidate, SIZE, source, len(raw), ctypes.byref(a), bar_base, bar_size) == 0
            assert candidate.raw == bytes(o.uc.mem_read(platform + 0x400, SIZE)) + b'\xa5' * 16
            cases.append(dict(bar2_gpa=hex(bar_base), bar2_bytes=hex(bar_size),
                              resource_flags=hex(flags), compared_bytes=SIZE,
                              subsequent_wddm_segments=segment_count, window_preserved=True))
    invalid = []
    for base, size in ((0, 1 << 34), (1, 1 << 34), (1 << 35, 0), (1 << 35, 1),
                       ((1 << 64) - 4096, 8192), (1 << 35, 4096)):
        ctypes.memset(candidate, 0xa5, SIZE + 16)
        assert lib.build(candidate, SIZE, source, len(raw), ctypes.byref(a), base, size) < 0
        assert candidate.raw == b'\xa5' * (SIZE + 16)
        invalid.append(dict(bar2_gpa=hex(base), bar2_bytes=hex(size), output_unchanged=True))
    report = dict(reference_sha256=o.pe.sha256, passed=True,
                  allocation_zeroed_bytes=0x3e48, initializer_hooked_after_zeroing=True,
                  resource_and_refresh_instructions_executed=True, cases=cases, rejected=invalid,
                  hardware_written=False, host_copy_length_verified=False,
                  limits='Constructor allocator/initializer binding modeled; resource decoding and refresh executed. Not the entire initialization callback chain or a lifetime/publication test.')
    (ROOT / 'reports/window-initialization-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: constructor zeroing, {len(cases)} resource/refresh cases, {len(invalid)} rejected apertures')


if __name__ == '__main__':
    main()
