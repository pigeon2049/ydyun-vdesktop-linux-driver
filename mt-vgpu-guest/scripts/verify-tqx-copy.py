#!/usr/bin/env python3
"""Compare TQX byte-copy geometry with restricted reference execution in RAM."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle
from tqx_texture_reference import TextureOracle
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RIP

ROOT = Path(__file__).resolve().parents[1]
x = ReferenceOracle()
texture_reference = TextureOracle()
x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
ranges = [(a, a + n) for a, n in (
    (0x1400a624c, 1899), (0x1400a5314, 470), (0x1400450c8, 109),
    (0x140045138, 83), (0x140056a00, 92), (0x1400a5740, 638),
    (0x1400a5ee0, 290), (0x1400571e0, 223), (0x140047680, 64),
    (0x1400475a4, 125))]
INPUT, CHUNKS, SURFACE, STATE = 0x200000, 0x201000, 0x202000, 0x203000

class Input(C.Structure):
    _fields_ = [('src', C.c_uint64), ('dst', C.c_uint64), ('bytes', C.c_uint64)]

class Chunk(C.Structure):
    _fields_ = [('offset', C.c_uint64)] + [(n, C.c_uint32) for n in
        ('element_bytes', 'layers', 'width', 'height')]

class Plan(C.Structure):
    _fields_ = Input._fields_ + [('count', C.c_uint32), ('reserved', C.c_uint32),
        ('operations', C.c_uint32 * 4), ('chunks', Chunk * 4), ('surfaces', (C.c_ubyte * 104) * 4),
        ('states', (C.c_ubyte * 168) * 4), ('source_descriptors', (C.c_ubyte * 64) * 4)]

libpath = ROOT / 'build/firmware/tqx-copy.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_copy_oracle_wrapper.c','-o',libpath], check=True)
lib = C.CDLL(str(libpath))
lib.build_plan.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(Input)]
rng = random.Random(0x222)
lengths = [1,2,3,15,16,17,4095,4096,4097,4111,4112,32767,32768,
           0x7ffffff,0x8000000,0x8000001,0xfffffff,0xfffffffe,0xffffffff]
cases = [(0x100000 + a,0x100000000 + (a * 7 % 16),n)
         for a in range(16) for n in lengths]
cases += [(rng.randrange(1,1<<35),rng.randrange(1<<36,1<<38),rng.randrange(1,1<<32))
          for _ in range(128)]
counts = {}
for src, dst, length in cases:
    req, p = Input(src,dst,length), Plan()
    assert lib.build_plan(C.byref(p), C.sizeof(p), C.byref(req)) == 0
    x.uc.mem_write(INPUT, struct.pack('<QQ',src,0))
    x.uc.mem_write(CHUNKS, bytes(0x1b0))
    count = x.run(0x1400a624c, [INPUT,length,1,1,CHUNKS], ranges)
    assert p.count == count and 1 <= count <= 4
    expected = bytes(x.uc.mem_read(CHUNKS,96))
    assert bytes(p.chunks) == expected, (src,length,bytes(p.chunks).hex(),expected.hex())
    assert bytes(x.uc.mem_read(CHUNKS+96,0x1b0-96)) == bytes(0x1b0-96)
    total = 0
    for i in range(count):
        c = p.chunks[i]
        assert c.offset == total and c.layers == 1
        assert 0 < c.width <= 32768 and 0 < c.height <= 32768
        total += c.element_bytes * c.width * c.height
        x.uc.mem_write(SURFACE,bytes([0xa5])*104)
        x.uc.mem_write(STATE,bytes([0xa5])*168)
        x.run(0x1400a5740,[CHUNKS+i*24,SURFACE,STATE],ranges)
        assert bytes(p.surfaces[i]) == bytes(x.uc.mem_read(SURFACE,104)), (src,length,i,'surface')
        assert bytes(p.states[i]) == bytes(x.uc.mem_read(STATE,168)), (src,length,i,'state')
        descriptor = texture_reference.run(src+c.offset,c.element_bytes,c.width,c.height)
        assert bytes(p.source_descriptors[i])[:48] == descriptor[:48]
        assert bytes(p.source_descriptors[i])[48:] == bytes(16)
    assert total == length
    counts[count] = counts.get(count,0) + 1

# Execute the enclosing single-region builder up to its job-emission boundary.
# Synchronization and emission are modeled; operation selection executes.
ranges += [(0x14010f034,0x14010f034+1187), (0x14004b2bc,0x14004b2bc+15),
           (0x140114418,0x140114418+3367)]
x.put64(0x140138420,0x2f8810)
x.hooks[0x2f8810] = lambda: x.uc.reg_write(UC_X86_REG_RIP,x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x1400a6168] = lambda: x.ret(0)
x.hooks[0x1400a8ce0] = x.ret
def compare_memory():
    a,b,n = [x.arg(i) for i in range(3)]
    assert n == 8
    aa,bb = bytes(x.uc.mem_read(a,n)),bytes(x.uc.mem_read(b,n))
    x.ret(((aa > bb) - (aa < bb)) & 0xffffffffffffffff)
x.hooks[0x140130e80] = compare_memory
captured = []
def capture_job():
    views = x.arg(2)
    records = []
    for v in (views,views+64):
        records.append((x.get64(v+16),bytes(x.uc.mem_read(x.get64(v),104)),
                        bytes(x.uc.mem_read(x.get64(v+8),168)),
                        bytes(x.uc.mem_read(x.get64(v+40),16))))
    captured.append((x.arg(3),records))
    x.ret()
x.hooks[0x140113c08] = capture_job
outer_cases = cases
operations = set()
for src,dst,length in outer_cases:
    req, p = Input(src,dst,length), Plan()
    assert lib.build_plan(C.byref(p),C.sizeof(p),C.byref(req)) == 0
    # Nonzero resource-relative offsets test caller address arithmetic too.
    x.put64(0x210008,src-7); x.put64(0x211008,dst-11)
    x.uc.mem_write(0x212000,struct.pack('<QQQ',7,11,length))
    captured.clear()
    x.run(0x14010f034,[0x214000,0x213000,0x210000,0x211000,1,0x212000],ranges)
    assert len(captured) == p.count
    for i, (operation,pair) in enumerate(captured):
        assert operation == p.operations[i]
        operations.add(operation)
        c = p.chunks[i]
        for base, actual in zip((dst,src),pair):
            assert actual == (base+c.offset,bytes(p.surfaces[i]),bytes(p.states[i]),
                              struct.pack('<4I',0,0,c.width,c.height))

# Reject without changing any output; also preserve bytes after the plan.
rejects = 0
out = (C.c_ubyte * (C.sizeof(Plan) + 16))()
req = Input(0x100000,0x200000,4097)
C.memset(out,0xa5,C.sizeof(out))
assert lib.build_plan(out,C.sizeof(out),C.byref(req)) == 0
assert bytes(out)[C.sizeof(Plan):] == bytes([0xa5])*16
saved = bytes(out)
for bad in (Input(0,0,0),Input(0,0,1<<32),Input(1<<40,0,1),
            Input(0,1<<40,1),Input((1<<40)-1,0,2),Input(0,(1<<40)-1,2),
            Input((1<<64)-1,0,1)):
    assert lib.build_plan(out,C.sizeof(out),C.byref(bad)) < 0 and bytes(out) == saved
    rejects += 1
assert lib.build_plan(out,C.sizeof(Plan)-1,C.byref(req)) < 0 and bytes(out) == saved
assert lib.build_plan(out,C.sizeof(out),None) < 0 and bytes(out) == saved
assert lib.build_plan(None,C.sizeof(out),C.byref(req)) < 0
rejects += 3
report = dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
              reference_sha256=x.pe.sha256,cases=len(cases),chunk_counts=counts,
              rejected_cases=rejects,outer_builder_cases=len(outer_cases),
              operations=sorted(operations),plan_bytes=C.sizeof(Plan),
              compared='24-byte chunks, complete 104-byte surface / 168-byte state, 48-byte source texture/sampler, addresses, rectangles and operation per chunk',
              scope='14010f034 byte-copy parameters {src,0}, length, unit=1, align=1; CPU preparation only',
              modeled_helpers=['stack cookie (preserves RAX)', 'memcpy/memset if reached',
                               'memcmp', 'source descriptor allocation/device fields',
                               'outer builder only: interval synchronization and job emission'],
              hardware_access=False,gpu_execution=False,
              remaining='TQX job emission, stream finalization, DMA export and live execution')
(ROOT/'reports/tqx-copy-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
