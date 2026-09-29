#!/usr/bin/env python3
"""Check Linux state storage planning against actual queue allocator/getter."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess
from tqx_engine_state_reference import EngineStateOracle

ROOT = Path(__file__).resolve().parents[1]
class Plan(C.Structure):
    _fields_ = [('encoded_va', C.c_uint64), ('required_bytes', C.c_uint32),
                ('allocation_bytes', C.c_uint32), ('reference_alignment', C.c_uint32),
                ('reserved', C.c_uint32)]

libpath = ROOT/'build/firmware/tqx-engine-state.so'
libpath.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared',
    '-fPIC', ROOT/'tests/tqx_dma_oracle_wrapper.c', '-o', libpath], check=True)
lib = C.CDLL(str(libpath))
lib.plan_state.argtypes = [C.POINTER(Plan), C.c_uint32, C.c_uint64]
x = EngineStateOracle()
cases = failures = offsets = 0
for cores in range(1, 9):
    for prefer in (False, True):
        for va in (0x40000000, 0x40020000, 0x100000000, 0x8040000000-4096):
            r = x.run(cores, va, prefer)
            # Compare the entire captured allocation request, not just size.
            expected = bytearray(0xb0)
            struct.pack_into('<QQ', expected, 0x10, cores*384, 128)
            struct.pack_into('<I', expected, 0x30, 3)
            struct.pack_into('<I', expected, 0x3c, 2 if prefer else 1)
            struct.pack_into('<I', expected, 0x40, 1 if prefer else 2)
            if prefer:
                struct.pack_into('<I', expected, 0x44, 2)
            assert r['descriptor'] == expected
            assert r['properties'] == struct.pack('<I', 0x10100) + bytes(44)
            assert r['wrapper'] == struct.pack('<4Q', x.RESOURCE, 0, cores*384, 128)
            assert r['extra'] == 0 and r['encoded'] == va
            p = Plan()
            assert lib.plan_state(C.byref(p), cores, va) == 0
            assert (p.encoded_va, p.required_bytes, p.allocation_bytes,
                    p.reference_alignment, p.reserved) == (va, cores*384, 4096, 128, 0)
            cases += 1
        for error in (1, 0x80000001):
            r = x.run(cores, 0x40020000, prefer, failure=error)
            assert r['wrapper'] == bytes(32) and r['encoded'] is None and r['extra'] == 0
            failures += 1
    for offset in (128, 256, 384, 4096):
        r = x.run(cores, 0x40020000, offset=offset)
        assert r['encoded'] == 0x40020000 + offset
        offsets += 1
# The original getter silently truncates high bits; Linux rejects such VAs.
assert x.run(1, (1 << 40) + 0x40020000)['encoded'] == 0x40020000
p = Plan.from_buffer_copy(b'\xa5' * C.sizeof(Plan)); saved = bytes(p)
bad = 0
for cores, va in [(0, 0x40020000), (9, 0x40020000), (0xffffffff, 0x40020000),
    (1, 0), (1, 0x40020080), (1, 0x40020001), (1, 0x3ffff000),
    (1, 0x8040000000), (1, 1 << 40), (1, (1 << 64)-4096)]:
    assert lib.plan_state(C.byref(p), cores, va) < 0 and bytes(p) == saved
    bad += 1
assert lib.plan_state(None, 1, 0x40020000) < 0
platform_cases = 0
for cores in range(1,9):
    for external in (False,True):
        for pattern in (0,0xa5):
            assert x.platform_state(cores,0x40020000,external,pattern)==(0x40020000,0x600000,0x5000,128)
            platform_cases += 1
report = dict(utc=datetime.now(timezone.utc).isoformat(), passed=True,
    reference_sha256=x.x.pe.sha256, full_allocation_cases=cases,
    allocation_failure_cases=failures, getter_offset_cases=offsets,
    original_truncation_cases=1, linux_rejections=bad+1, platform_preservation_cases=platform_cases,
    executed=['093aa8 allocation request', '04c490 resource wrapper',
              '093cc8/04c288/04b2bc GPU address getter',
              '0931b8 type2 selection and 0219d0/041cc4 existing ctxState callback'],
    modeled=['05aec0 allocator success/failure and resource VA',
             'explicit core counts 1..8 and physical heap availability',
             'two pre-existing platform context-state descriptors and RAM backing'],
    hardware_access=False, gpu_execution=False,
    limits='Storage plan only. Does not establish initial state contents, live topology, BO ownership, or firmware submission.')
(ROOT/'reports/tqx-engine-state-validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
