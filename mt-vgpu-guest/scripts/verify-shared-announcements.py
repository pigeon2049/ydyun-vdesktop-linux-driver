#!/usr/bin/env python3
"""Execute reference info-request/announcement path with all I/O intercepted."""
import ctypes
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/host-query'
BUILD.mkdir(parents=True, exist_ok=True)
(BUILD / 'announce.c').write_text('#include "mt_guest_announcements.h"\n'
    'int build(const void *i,u32 n,u64 b,u64 s,u8 *o) '
    '{ return mt_shared_announcements(i,n,b,s,o); }\n')
subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
               '-I', str(ROOT / 'kernel'), str(BUILD / 'announce.c'),
               '-o', str(BUILD / 'announce.so')], check=True)
fn = ctypes.CDLL(str(BUILD / 'announce.so')).build
fn.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p]
oracle = ReferenceOracle()
CTX, INFO, INFO_GPA = 0x210000, 0x218000, 0x12345000
oracle.put64(CTX + 0x1cb0, INFO)
oracle.put32(CTX + 0x1cc0, 4096)
oracle.put64(CTX + 0x1d18, 0x220000)
oracle.put64(0x1401381b8, 0x2f8000)
oracle.hooks[0x2f8000] = lambda: oracle.ret(INFO_GPA)
trace, records = [], []
info = b''

def io():
    assert oracle.arg(0) == CTX
    offset, value = oracle.arg(1) & 0xffffffff, oracle.arg(2)
    trace.append((offset, value))
    if offset == 0xc8:
        assert value == INFO_GPA and oracle.get64(INFO + 0xc48) == 3
        oracle.uc.mem_write(INFO, info)
    oracle.ret()

def notify():
    assert (oracle.arg(0), oracle.arg(1), oracle.arg(3)) == (0x220000, 0, 3)
    records.append(bytes(oracle.uc.mem_read(oracle.arg(2), 96)))
    oracle.ret(0)

oracle.hooks[0x140027780] = io
oracle.hooks[0x14002b96c] = notify
base_info = (ROOT / 'reports/device-info.bin').read_bytes()
rng = random.Random(275)
cases = [(0x800000000, 0x43000000, 0x200000)]
cases += [(rng.randrange(1, 1 << 24) << 12, 0x43000000 + (rng.randrange(1024) << 12),
           rng.choice([0x200000, 0x400000])) for _ in range(64)]
for bar, offset, size in cases:
    data = bytearray(base_info)
    struct.pack_into('<QQ', data, 0x28 + 5 * 24, offset, size)
    info = bytes(data)
    oracle.put64(CTX + 0x400, bar)
    oracle.put64(CTX + 0x518, 0)
    trace.clear()
    records.clear()
    result = oracle.run(0x140026b30, [CTX], [(0x140026b30, 0x140026d20),
                        (0x140130c50, 0x140130c70)])
    assert result == 0
    assert trace == [(0xc8, INFO_GPA), (0x20, bar), (0x28, 0)]
    out = ctypes.create_string_buffer(96)
    assert fn(info, len(info), bar, 1 << 34, out) == 0
    # Reference stack reserved fields are undefined; compare semantic fields.
    expected = records[0]
    for idx, value in enumerate([0, bar + offset, 0x200000]):
        at = idx * 32
        assert expected[at:at + 10] == out.raw[at:at + 10]
        assert out.raw[at:at + 8] == struct.pack('<Q', value)
        assert out.raw[at + 8:at + 12] == bytes([3, idx, 0, 0])
        assert out.raw[at + 12:at + 32] == bytes(20)

bad = []
for mutate in ['short-shared', 'no-shared', 'outside-bar', 'legacy', 'overflow-gpa', 'short-info']:
    data = bytearray(base_info)
    bar, length = 0x800000000, len(data)
    if mutate == 'short-shared': struct.pack_into('<Q', data, 0x28 + 5 * 24 + 8, 0x1000)
    if mutate == 'no-shared': struct.pack_into('<Q', data, 0x28 + 5 * 24 + 16, 0)
    if mutate == 'outside-bar': struct.pack_into('<Q', data, 0x28 + 5 * 24, 1 << 34)
    if mutate == 'legacy': struct.pack_into('<Q', data, 0x10, 0x351)
    if mutate == 'overflow-gpa': bar = (1 << 64) - 4096
    if mutate == 'short-info': length = 0xcc7
    out = ctypes.create_string_buffer(b'Z' * 96, 96)
    assert fn(bytes(data), length, bar, 1 << 34, out) < 0
    assert out.raw == b'Z' * 96
    bad.append(mutate)
report = {'reference_sha256': oracle.pe.sha256, 'reference': '140026b30',
          'matching_cases': len(cases), 'invalid_unchanged': bad,
          'current_messages': [{'type': 3, 'subtype': i, 'operation': 0, 'value': hex(v)}
                               for i, v in enumerate([0, 0x843000000, 0x200000])],
          'hardware_written': False, 'passed': True}
(ROOT / 'reports/shared-announcements-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
