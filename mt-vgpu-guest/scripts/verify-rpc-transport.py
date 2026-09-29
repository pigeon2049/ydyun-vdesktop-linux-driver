#!/usr/bin/env python3
"""Exercise actual kernel transport C on RAM with MMIO/barriers intercepted.

Exhaustive ring cursor/space cases plus reference-instruction IRQ checks.
This cannot verify hardware memory ordering or the Linux IRQ/work lifecycle.
"""
import ctypes as C
import errno
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/rpc-transport'
SHIM = BUILD / 'include/linux'
SHIM.mkdir(parents=True, exist_ok=True)
(SHIM / 'io.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define __iomem
#define READ_ONCE(v) (*(volatile __typeof__(v) *)&(v))
#define WRITE_ONCE(v,x) (*(volatile __typeof__(v) *)&(v) = (x))
#define mb() __sync_synchronize()
#define rmb() mb()
#define wmb() mb()
extern unsigned notifications;
static inline void writeq(u64 v, void *p) { memcpy(p, &v, 8); notifications++; }
''')
(SHIM / 'types.h').write_text('#include "io.h"\n')
(SHIM / 'string.h').write_text('#include <string.h>\n')
# Do not include glibc errno.h from a linux/errno.h shim (it recurses).
(SHIM / 'errno.h').write_text('\n'.join(f'#define {n} {getattr(errno,n)}' for n in
    ['EINVAL', 'EPROTO', 'EAGAIN', 'EOPNOTSUPP', 'ENOSPC']) + '\n')
(BUILD / 'transport.c').write_text('''#include "mt_rpc_transport.h"
#include "mt_rpc_publish.h"
unsigned notifications;
int ack(void *p) { return mt_rpc_ack(p); }
int answer(void *p, void *r, u64 n, u32 u, u32 *s) {
 notifications=0; return mt_rpc_answer_queries(p,r,n,u,0,s);
}
int answer_stat2(void *p, void *r, u64 n, u32 u, u64 x, u32 *s) {
 notifications=0; return mt_rpc_answer_queries(p,r,n,u,x,s);
}
int publish(void *p, void *r, u32 i, void *d, u32 n) {
 notifications=0; return mt_rpc_publish(p,r,i,d,n);
}
''')
subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-D__KERNEL__', '-shared', '-fPIC',
    '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I', str(SHIM.parent),
    '-I', str(ROOT / 'kernel'), str(BUILD / 'transport.c'), '-o', str(BUILD / 'transport.so')], check=True)
lib = C.CDLL(str(BUILD / 'transport.so'))
lib.ack.argtypes = [C.c_void_p]
lib.answer.argtypes = [C.c_void_p, C.c_void_p, C.c_uint64, C.c_uint32, C.POINTER(C.c_uint32)]
lib.answer_stat2.argtypes = [C.c_void_p, C.c_void_p, C.c_uint64, C.c_uint32,
                             C.c_uint64, C.POINTER(C.c_uint32)]
lib.publish.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
notify = C.c_uint.in_dll(lib, 'notifications')
rng = random.Random(275)
regs = C.create_string_buffer(4096)
records = rng.randbytes(15 * 32)
published = answered = invalid = 0
for index in (0, 2):
    for head in range(16):
        for tail in range(16):
            for count in range(1, 16):
                before = bytearray(b'Z' * 4096)
                at = index * 0x210
                before[at:at + 2] = bytes([head, tail])
                page = C.create_string_buffer(bytes(before), 4096)
                free = (tail - head - 1) & 15
                ret = lib.publish(page, regs, index, records, count)
                expected = before.copy()
                if count > free:
                    assert ret == -errno.EAGAIN and notify.value == 0
                else:
                    assert ret == 0 and notify.value == 1
                    assert struct.unpack_from('<Q', regs.raw, 0x138)[0] == index
                    for i in range(count):
                        slot = at + 16 + ((head + i) & 15) * 32
                        expected[slot:slot + 32] = records[i * 32:(i + 1) * 32]
                    expected[at] = (head + count) & 15
                assert page.raw == expected
                published += 1

# Exercise wrap, partial completion, full output, and accounting values.
for start in range(16):
    for output in range(16):
        for free in range(16):
            for pending in (1, 15):
                before = bytearray(b'Z' * 4096)
                before[0x210:0x212] = bytes([(start + pending) & 15, start])
                before[0x420:0x422] = bytes([output, (output + free + 1) & 15])
                stat2 = rng.getrandbits(64)
                for i in range(pending):
                    slot = 0x220 + ((start + i) & 15) * 32
                    before[slot + 8], before[slot + 9], before[slot + 11] = 1, i % 3, 1
                page = C.create_string_buffer(bytes(before), 4096)
                sent = C.c_uint32(999)
                size, util = rng.getrandbits(64), rng.getrandbits(32)
                ret = lib.answer_stat2(page, regs, size, util, stat2, C.byref(sent))
                n = min(free, pending)
                assert sent.value == notify.value == n
                assert ret == (-errno.ENOSPC if n < pending else 0)
                expected = before.copy()
                for i in range(n):
                    slot = 0x430 + ((output + i) & 15) * 32
                    value = (util, size, stat2)[i % 3]
                    expected[slot:slot + 32] = struct.pack('<QBBBB20x', value, 1, i % 3, 0, 2)
                expected[0x211] = (start + n) & 15
                expected[0x420] = (output + n) & 15
                assert page.raw == expected
                answered += 1

# Invalid input/output cursors and unknown message must not consume a record.
for pos in (0x210, 0x211, 0x420, 0x421):
    for value in (16, 255):
        page = C.create_string_buffer(4096)
        page[0x210] = 1
        page[0x228], page[0x229], page[0x22b] = 1, 1, 1
        page[pos] = value
        before = page.raw
        sent = C.c_uint32(999)
        assert lib.answer(page, regs, 42, 0, C.byref(sent)) == -errno.EPROTO
        assert page.raw == before and sent.value == notify.value == 0
        invalid += 1
for pos, value in ((8, 0), (9, 3), (9, 255), (11, 0), (11, 2)):
    page = C.create_string_buffer(4096)
    page[0x210] = 1
    page[0x228], page[0x229], page[0x22b] = 1, 1, 1
    page[0x220 + pos] = value
    before = page.raw
    sent = C.c_uint32(999)
    assert lib.answer(page, regs, 42, 0, C.byref(sent)) == -errno.EOPNOTSUPP
    assert page.raw == before and sent.value == notify.value == 0
    invalid += 1
for index, count in ((1, 1), (3, 1), (0xffffffff, 1), (0, 0), (0, 16)):
    page = C.create_string_buffer(4096)
    before = page.raw
    assert lib.publish(page, regs, index, records, count) == -errno.EINVAL
    assert page.raw == before and notify.value == 0
    invalid += 1
for which in range(3):
    args = [C.create_string_buffer(4096), regs, 0, records, 1]
    args[(0, 1, 3)[which]] = None
    assert lib.publish(*args) == -errno.EINVAL and notify.value == 0
    invalid += 1
for pos in (0, 1):
    page = C.create_string_buffer(4096)
    page[pos] = 255
    before = page.raw
    assert lib.publish(page, regs, 0, records, 1) == -errno.EPROTO
    assert page.raw == before and notify.value == 0
    invalid += 1

oracle = ReferenceOracle()
ctx, shared = 0x210000, 0x215000
oracle.put64(ctx + 0x1cd0, shared)
ack_cases = 0
for value in [0, 1, 0xffffffff, 0x100000000, (1 << 64) - 1] + [rng.getrandbits(64) for _ in range(128)]:
    raw = bytearray(rng.randbytes(64))
    struct.pack_into('<IQ', raw, 4, 1, value)
    oracle.uc.mem_write(shared, bytes(raw))
    oracle.run(0x140023810, [ctx], [(0x140023810, 0x140023830)])
    page = C.create_string_buffer(bytes(raw), 64)
    assert lib.ack(page) == 1
    assert page.raw == bytes(oracle.uc.mem_read(shared, 64))
    # Already acknowledged => no mutation, do not claim another device's IRQ.
    before = page.raw
    assert lib.ack(page) == 0 and page.raw == before
    ack_cases += 1
for state in (0, 2, 3, 0xffffffff):
    page = C.create_string_buffer(64)
    struct.pack_into('<I', page, 4, state)
    before = page.raw
    assert lib.ack(page) == 0 and page.raw == before
    invalid += 1
report = dict(batch_cursor_cases=published, query_cursor_cases=answered,
    invalid_unchanged_cases=invalid, reference_irq_cases=ack_cases,
    reference_sha256=oracle.pe.sha256, hardware_written=False,
    undefined_behavior_sanitizer=True, passed=True,
    limits=['RAM/MMIO shim does not verify hardware ordering', 'IRQ/work scheduling is not simulated'])
(ROOT / 'reports/rpc-transport-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
