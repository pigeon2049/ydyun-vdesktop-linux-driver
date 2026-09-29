#!/usr/bin/env python3
"""Execute original record export, paging adaptation and single-job normalization.

All object and CPU/GPU mappings are RAM fixtures. No original allocator, OS
submission, queue publication or hardware access is run.
"""
import ctypes as C
import json
import random
import runpy
import struct
import subprocess
from pathlib import Path
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RIP, UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]
x = ReferenceOracle()
OBJ, RECORD, EXPORT, CONTEXT, DMA, STATE = (0x200000, 0x202000, 0x203000, 0x204000, 0x205000, 0x206000)
x.put64(OBJ, 0x140bc1100)  # Actual CE3 command vtable, +30 -> 1400e2690.
x.put64(OBJ + 0xb60 + 0x408, RECORD)
x.put32(OBJ + 0xb60 + 0x410, 1)
x.put64(0x140138420, 0x2f8810)
x.hooks[0x2f8810] = lambda: x.uc.reg_write(UC_X86_REG_RIP, x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140130c50] = x.ret
x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
ranges = [(0x1400e2690, 0x1400e2804), (0x14009419c, 0x1400941b0),
          (0x14004c67c, 0x14004c6e8), (0x14002171c, 0x140021798),
          (0x140019f10, 0x14001a064)]


def reference(record, dma_va, state_va, variant, offset):
    x.uc.mem_write(RECORD, record)
    x.uc.mem_write(EXPORT, b'\xa5' * 64)
    assert x.run(0x1400e2690, [OBJ, EXPORT], ranges) == 0
    exported = bytes(x.uc.mem_read(EXPORT, 64))
    assert exported[48:] == b'\xa5' * 16
    x.uc.mem_write(DMA, bytes(0x150) + b'\xa5' * 16)
    x.uc.mem_write(STATE, b'\xa5' * offset + bytes(128) + b'\xa5' * 16)
    x.uc.mem_write(CONTEXT, bytes(0x180))
    x.put64(CONTEXT + 0x138, DMA)
    x.put64(CONTEXT + 0x140, dma_va)
    x.put64(CONTEXT + 0x160, STATE)
    x.put64(CONTEXT + 0x168, (state_va - offset) & ((1 << 64) - 1))
    x.put32(CONTEXT + 0x170, offset)
    x.put32(CONTEXT + 0x174, variant)
    x.run(0x14002171c, [OBJ, DMA], ranges)
    # Execute the actual indirect export inside the adapter, not a modeled call.
    x.run(0x140019f10, [CONTEXT, dma_va, 0x150], ranges)
    assert bytes(x.uc.mem_read(DMA + 0x150, 16)) == b'\xa5' * 16
    assert bytes(x.uc.mem_read(STATE, offset)) == b'\xa5' * offset
    assert bytes(x.uc.mem_read(STATE + offset + 128, 16)) == b'\xa5' * 16
    advanced = struct.unpack('<I', x.uc.mem_read(CONTEXT + 0x170, 4))[0]
    assert advanced == offset + (128 if variant == 1 else 32)
    return exported[:48], bytes(x.uc.mem_read(DMA, 0x150)) + bytes(x.uc.mem_read(STATE + offset, 128))


libpath = ROOT / 'build/firmware/ce-paging.so'
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared', '-fPIC',
                ROOT / 'tests/ce_paging_oracle_wrapper.c', '-o', libpath], check=True)
lib = C.CDLL(str(libpath))
lib.export_one.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
lib.encode.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32, C.c_uint64]
out = C.create_string_buffer(480)
exported = C.create_string_buffer(64)
rng = random.Random(3000004)
cases = 0
for variant in (0, 1, 3):
    for i in range(64):
        record = rng.randbytes(256)
        state_va = rng.getrandbits(64) & ~31
        dma_va = rng.getrandbits(64) & ~31
        offset = (i % 4) * 128
        expected_export, expected = reference(record, dma_va, state_va, variant, offset)
        C.memset(out, 0xa5, 480)
        C.memset(exported, 0xa5, 64)
        assert lib.export_one(exported, 64, record, len(record)) == 0
        assert exported.raw == expected_export + b'\xa5' * 16
        assert lib.encode(out, 480, record, len(record), state_va) == 0
        assert out.raw == expected + b'\xa5' * 16
        cases += 1

# Invalid shape/capacity must not modify output. Wide raw addresses above test
# serialization only: the VM-backed prepare path imposes 40-bit address limits.
saved = out.raw
for ptr, size, cap in ((None, 256, 480), (record, 255, 480), (record, 257, 480), (record, 256, 463)):
    assert lib.encode(out, cap, ptr, size, state_va) == -22
    assert out.raw == saved
assert lib.encode(None, 480, record, 256, state_va) == -22
saved_export = exported.raw
assert lib.export_one(exported, 47, record, 256) == -22
assert exported.raw == saved_export

# Reuse original instruction-produced streams, not the C encoder's bytes, as
# the input to the original adapter/normalizer. Compare the complete bundle.
fixtures = runpy.run_path(str(ROOT / 'scripts/verify-ce-stream.py'))['sequence_fixtures']
class Copy(C.Structure):
    _fields_ = [('src', C.c_uint64), ('dst', C.c_uint64), ('bytes', C.c_uint64),
                ('version', C.c_uint32), ('flags', C.c_uint32)]
class Stream(C.Structure):
    _fields_ = [('copy', Copy), ('command_va', C.c_uint64)]
class Paging(C.Structure):
    _fields_ = [('stream', Stream), ('descriptor_va', C.c_uint64), ('state_va', C.c_uint64)]
lib.prepare.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(Paging)]
bundle = C.create_string_buffer(912)
for source, dest, size, flags, command_va, sequence in fixtures:
    dma_va = (command_va & ~4095) + 4096
    state_va = dma_va + 512
    _, paging = reference(sequence[176:], dma_va, state_va, 1, 0)
    req = Paging(Stream(Copy(source, dest, size, 3, flags), command_va), dma_va, state_va)
    C.memset(bundle, 0xa5, 912)
    assert lib.prepare(bundle, 912, C.byref(req)) == 0
    assert bundle.raw == sequence + paging + b'\xa5' * 16

report = dict(passed=True, reference_sha256=x.pe.sha256,
              record_exports=cases, paging_adapter_and_normalization=cases,
              complete_stream_to_paging_sequences=len(fixtures),
              reference_functions=['1400e2690', '14002171c', '140019f10', '14009419c', '14004c67c'],
              descriptor_bytes=336, reserved_state_bytes=128,
              state_bytes_written_by_reference=16, hardware_accessed=False,
              assumptions=['exactly one software record', 'zero-initialized 336-byte descriptor',
                           'zero-initialized state slot', 'RAM CPU/GPU address translation fixtures'],
              limits='Windows paging representation only. Other queue paths, live CE version, memory attributes, complete submission metadata, and firmware execution are not established.')
(ROOT / 'reports/ce-paging-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
