#!/usr/bin/env python3
"""Compare host-stat replies and IRQ ack with Windows instructions.

No driver entry point, imported OS code, MMIO or actual device is executed.
"""
import ctypes
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / 'build/host-query'
build.mkdir(parents=True, exist_ok=True)
source = build / 'query.c'
source.write_text('#include "mt_host_query.h"\n'
                  'int reply(const u8 *r, u64 n, u32 u, u8 *o) '
                  '{ return mt_host_memory_reply(r,n,u,0,o); }\n'
                  'int reply_stat2(const u8 *r, u64 n, u32 u, u64 x, u8 *o) '
                  '{ return mt_host_memory_reply(r,n,u,x,o); }\n')
subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                '-I', str(ROOT / 'kernel'), str(source), '-o', str(build / 'query.so')], check=True)
fn = ctypes.CDLL(str(build / 'query.so')).reply
fn.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_void_p]
fn2 = ctypes.CDLL(str(build / 'query.so')).reply_stat2
fn2.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32,
                ctypes.c_uint64, ctypes.c_void_p]
oracle = ReferenceOracle()
request, reply, context, shared = 0x210000, 0x211000, 0x212000, 0x215000
oracle.put64(context + 0x1cc8, reply + 0x100)
rng = random.Random(275)
cases = [0, 1, 0xffffffff, 0x100000000, (1 << 64) - 1]
cases += [rng.getrandbits(64) for _ in range(128)]
for value in cases:
    raw = bytearray(rng.randbytes(32))
    raw[8], raw[9], raw[11] = 1, 1, 1
    oracle.uc.mem_write(request, bytes(raw))
    oracle.uc.mem_write(reply, bytes([0xa5]) * 32)
    oracle.put64(context + 0x1d98, value)
    oracle.run(0x14002729c, [request, reply, reply + 10, context],
               [(0x14002729c, 0x14002734f)])
    expected = bytes(oracle.uc.mem_read(reply, 32))
    output = ctypes.create_string_buffer(32)
    assert fn(bytes(raw), value, 0, output) == 0
    assert output.raw[:8] == expected[:8] == struct.pack('<Q', value)
    assert output.raw[10] == expected[10] == 0
    assert output.raw[8:12] == bytes([1, 1, 0, 2])
    assert output.raw[12:] == bytes(20)

    # Subtype 2 returns the resolved second EscapeSetGpuUtilStat value.
    # The helper's caller owns the +0x1d90 fallback decision.
    for fallback in (0, rng.getrandbits(64)):
        stat2 = value
        resolved = stat2 if stat2 else fallback
        raw[9] = 2
        oracle.uc.mem_write(request, bytes(raw))
        oracle.put64(context + 0x1da8, stat2)
        oracle.put64(context + 0x1d90, fallback)
        oracle.run(0x14002729c, [request, reply, reply + 10, context],
                   [(0x14002729c, 0x14002734f)])
        expected = bytes(oracle.uc.mem_read(reply, 32))
        assert fn2(bytes(raw), 0, 0, resolved, output) == 0
        assert output.raw[:8] == expected[:8] == struct.pack('<Q', resolved)
        assert output.raw[8:12] == bytes([1, 2, 0, 2])
        assert oracle.uc.mem_read(reply + 0x138, 1)[0] == (1 if stat2 else 0)

    raw[9] = 0
    oracle.uc.mem_write(request, bytes(raw))
    oracle.put64(context + 0x1da0, value)
    oracle.run(0x14002729c, [request, reply, reply + 10, context],
               [(0x14002729c, 0x14002734f)])
    expected = bytes(oracle.uc.mem_read(reply, 32))
    assert fn(bytes(raw), 123, value & 0xffffffff, output) == 0
    assert output.raw[:8] == expected[:8] == struct.pack('<Q', value & 0xffffffff)
    assert output.raw[8:12] == bytes([1, 0, 0, 2])

    # 140023810 writes shared+4=2 and increments shared+8.
    initial = bytearray(rng.randbytes(64))
    struct.pack_into('<IQ', initial, 4, 1, value)
    oracle.uc.mem_write(shared, bytes(initial))
    oracle.put64(context + 0x1cd0, shared)
    oracle.run(0x140023810, [context], [(0x140023810, 0x140023830)])
    struct.pack_into('<IQ', initial, 4, 2, (value + 1) & ((1 << 64) - 1))
    assert bytes(oracle.uc.mem_read(shared, 64)) == initial

invalid = 0
for field in [8, 9, 11]:
    for value in [0, 2, 3, 255]:
        if field == 9 and value in (0, 1, 2):
            continue
        raw = bytearray(32)
        raw[8], raw[9], raw[11] = 1, 1, 1
        raw[field] = value
        output = ctypes.create_string_buffer(bytes([0xa5]) * 32, 32)
        assert fn(bytes(raw), 123, 0, output) == -1
        assert output.raw == bytes([0xa5]) * 32
        invalid += 1
report = {'reference_query': '14002729c', 'reference_irq_ack': '140023810',
          'query_instruction_cases': len(cases) * 2 + len(cases) * 2,
          'ack_instruction_cases': len(cases),
          'unhandled_requests_preserved': invalid, 'passed': True,
          'scope': 'Query callback output and reference ack semantics; not IRQ delivery or firmware startup'}
(ROOT / 'reports/host-query-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
