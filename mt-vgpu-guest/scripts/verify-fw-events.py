#!/usr/bin/env python3
"""Compare dispatch, normal completion matching and empty packets with PE instructions.

All reference execution is RAM-only with an allowlist; OS notification, diagnostic
history and queue submission are captured helpers, never Windows or device calls.
"""
import ctypes as C
import errno
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
ADAPTER, PLATFORM, QUEUE = 0x200000, 0x210000, 0x400000
NODE, ENTRIES, EVENT = 0x221000, 0x240000, 0x250000
CTX, MODE, GROUP = 0x220000, 0x201000, 0x260000
SIZE, STRIDE = 0x11520, 0x2e30


def main():
    target = ROOT / 'build/firmware/events.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared',
                    '-fPIC', ROOT / 'tests/fw_event_oracle_wrapper.c', '-o', target], check=True)
    lib = C.CDLL(str(target))
    p, u = C.c_void_p, C.c_uint32
    lib.drain.argtypes = [p, u, u, u, p, p]
    lib.matches.argtypes = [p, u, u]
    lib.marker.argtypes = [p, u]
    lib.paused.argtypes = [u]
    oracle = ReferenceOracle()
    rng = random.Random(27564)
    data = C.create_string_buffer(SIZE)
    records = (u * (6 * 63 * 8))()
    count = u()
    dispatches = []
    callbacks = (0x14000e6a4, 0x14000e7a4, 0x14000e784, 0x14000e5c8)
    osid = 4
    oracle.put64(PLATFORM + 0x20, QUEUE)
    oracle.uc.mem_write(PLATFORM + 0x48, bytes([osid]))
    for dm in range(6):
        oracle.put64(ADAPTER + 0x2e8 + osid * 0x30 + dm * 8, CTX + dm * 0x100)
    def capture(kind):
        assert oracle.arg(0) == ADAPTER
        ctx = oracle.arg(1)
        dm = (ctx - CTX - 0x28) // 0x100
        assert ctx == CTX + dm * 0x100 + 0x28 and 0 <= dm < 6
        dispatches.append((dm, kind, *struct.unpack('<6I', oracle.uc.mem_read(oracle.arg(2), 24))))
        oracle.ret()
    for kind, addr in enumerate(callbacks):
        oracle.hooks[addr] = lambda kind=kind: capture(kind)
    dispatch_cases = 0
    for active_dm in range(6):
        for tail in range(64):
            before = bytearray(rng.randbytes(SIZE))
            for dm in range(6):
                start = (tail + dm) & 63
                n = (tail % 63) + 1 if dm == active_dm else 0
                cursor = dm * STRIDE + 0x2e20
                struct.pack_into('<I4xI', before, cursor, (start + n) & 63, start)
                for i in range(n):
                    offset = dm * STRIDE + 0x2800 + ((start + i) & 63) * 24
                    typ = (0, 5, 0x50, 0x101, 7, 0xffffffff)[i % 6]
                    struct.pack_into('<6I', before, offset, rng.getrandbits(32), typ,
                                     rng.getrandbits(32), 11, 12, 13)
            oracle.uc.mem_write(QUEUE, bytes(before))
            dispatches.clear()
            oracle.run(0x14000be34, [ADAPTER, PLATFORM], [(0x14000be34, 0x14000bf20)])
            C.memmove(data, bytes(before), SIZE)
            assert lib.drain(data, SIZE, 378, 999, records, C.byref(count)) == 0
            expected = [tuple(records[i * 8:i * 8 + 8]) for i in range(count.value)
                        if records[i * 8 + 1] != 4]
            assert dispatches == expected
            assert data.raw == bytes(oracle.uc.mem_read(QUEUE, SIZE))
            dispatch_cases += 1

    # Matching normal completion executes the real handler. Imported notification
    # and diagnostic history append are modeled, retaining their arguments.
    for addr in callbacks:
        del oracle.hooks[addr]
    oracle.put64(ADAPTER, MODE)
    oracle.uc.mem_write(MODE, b'\x01')
    oracle.put64(ADAPTER + 0x1d0, 0x2f8100)
    oracle.put64(0x140138420, 0x2f8100)  # Model the guarded callback target.
    oracle.put64(CTX + 0x10, NODE)
    oracle.put64(CTX + 0x20, ENTRIES)
    oracle.put32(CTX, 3)
    oracle.put32(NODE + 0x0c, 9)
    oracle.put32(NODE + 0x74, 2)
    oracle.put32(ADAPTER + 0x42c, 64)
    notices, traces, diagnostics = [], [], []
    def notify():
        notices.append(bytes(oracle.uc.mem_read(oracle.arg(1), 0x50)))
        oracle.ret()
    def zero():
        oracle.uc.mem_write(oracle.arg(0), bytes(oracle.arg(1)))
        oracle.ret()
    oracle.hooks[0x2f8100] = notify
    oracle.hooks[0x1400083b0] = zero
    oracle.hooks[0x14000e8ac] = lambda: (traces.append(oracle.arg(2)), oracle.ret())
    oracle.hooks[0x14000c818] = lambda: (diagnostics.append(tuple(oracle.arg(i) for i in range(1, 4))), oracle.ret())
    event = C.create_string_buffer(24)
    matching_cases = 0
    for head in (0, 1, 62, 63):
        for pending in (False, True):
            for expected in (0, 1, 0x7fffffff, 0x80000000, 0xffffffff):
                for same in (False, True):
                    wire = expected if same else (expected + 1) & 0xffffffff
                    blob = struct.pack('<6I', 0xdeadbeef, 0, wire, 1, 2, 3)
                    oracle.uc.mem_write(EVENT, blob)
                    C.memmove(event, blob, 24)
                    oracle.put32(CTX + 0x28, head)
                    oracle.put32(CTX + 0x2c, (head + int(pending)) & 63)
                    oracle.put32(CTX + 8, 0x12345678)
                    oracle.put32(ENTRIES + head * 0x98 + 8, expected)
                    notices.clear(); traces.clear(); diagnostics.clear()
                    oracle.run(0x14000e6a4, [ADAPTER, CTX, EVENT], [(0x14000e6a4, 0x14000e782)])
                    accepted = pending and same
                    assert (lib.matches(event, int(pending), expected) == 0) == accepted
                    assert len(notices) == len(traces) == len(diagnostics) == int(accepted)
                    new_head = struct.unpack('<I', oracle.uc.mem_read(CTX + 0x28, 4))[0]
                    assert new_head == ((head + 1) & 63 if accepted else head)
                    if accepted:
                        assert struct.unpack_from('<5I', notices[0]) == (1, 0, wire, 9, 3)
                        assert traces == [EVENT] and diagnostics == [(2, wire, 0)]
                    matching_cases += 1

    # Empty submission packet captured before reference enqueue/MMIO.
    oracle.put64(ADAPTER + 0x420, GROUP)
    oracle.put64(GROUP + 0xe0, QUEUE)
    oracle.uc.mem_write(NODE + 0x70, b'\x00')
    oracle.hooks[0x1400238cc] = lambda: oracle.ret(0)
    oracle.hooks[0x14000c92c] = oracle.ret  # Diagnostic head/fence bookkeeping.
    oracle.hooks[0x140130c50] = oracle.ret  # Stack cookie check, modeled only.
    packets = []
    def packet():
        assert oracle.arg(3) == 2 and oracle.arg(5) == 0
        packets.append(bytes(oracle.uc.mem_read(oracle.arg(4), 80)))
        oracle.ret()
    oracle.hooks[0x14000bf20] = packet
    command = C.create_string_buffer(80)
    for wire in [0, 1, 0xffffffff] + [rng.getrandbits(32) for _ in range(61)]:
        packets.clear()
        oracle.run(0x14001475c, [ADAPTER, 0, NODE, 0, 0, wire, 0, 0],
                   [(0x14001475c, 0x140014900)])
        lib.marker(command, wire)
        assert packets == [command.raw]

    # Linux-only bounds/backpressure policy: reference ignores unknown events;
    # our caller sees all events and may leave a rejected record unacknowledged.
    before = bytearray(SIZE)
    struct.pack_into('<I4xI', before, 0x2e20, 2, 63)
    for pos in (63, 0, 1):
        struct.pack_into('<6I', before, 0x2800 + pos * 24, 0, 0, pos, 0, 0, 0)
    C.memmove(data, bytes(before), SIZE)
    assert lib.drain(data, SIZE, 378, 1, records, C.byref(count)) == -errno.EAGAIN
    assert count.value == 1 and struct.unpack_from('<I', data.raw, 0x2e28)[0] == 0
    assert lib.drain(data, SIZE, 1, 999, records, C.byref(count)) == 0 and count.value == 1
    assert struct.unpack_from('<I', data.raw, 0x2e28)[0] == 1
    assert lib.drain(data, SIZE, 378, 999, records, C.byref(count)) == 0 and count.value == 1
    rejects = 0
    for offset in (0x2e20, 0x2e28):
        for bad in (64, 0xffffffff):
            invalid = bytearray(SIZE)
            struct.pack_into('<I', invalid, offset, bad)
            C.memmove(data, bytes(invalid), SIZE)
            assert lib.drain(data, SIZE, 378, 999, records, C.byref(count)) == -errno.EIO
            assert count.value == 0 and data.raw == invalid
            rejects += 1
    for typ in (5, 0x50, 0x101, 7):
        C.memmove(event, struct.pack('<6I', 0, typ, 42, 0, 0, 0), 24)
        assert lib.matches(event, 1, 42) == -errno.EOPNOTSUPP
        rejects += 1
    # Runtime gate read from shared page 0. It is not a firmware-start request.
    del oracle.hooks[0x1400238cc]
    oracle.put64(PLATFORM + 0x1cd0, 0x270000)
    pause_cases = 0
    for flag in (0, 1, 2, 0x100, 0x80000001, 0xffffffff):
        oracle.put32(0x270000 + 0x330, flag)
        actual = oracle.run(0x1400238cc, [PLATFORM], [(0x1400238cc, 0x1400238ed)]) & 255
        assert actual == lib.paused(flag)
        pause_cases += 1
    for pointer in (0, PLATFORM):
        oracle.put64(PLATFORM + 0x1cd0, 0)
        assert oracle.run(0x1400238cc, [pointer], [(0x1400238cc, 0x1400238ed)]) & 255 == 0
        pause_cases += 1
    report = dict(reference_sha256=hashlib.sha256(oracle.pe.data).hexdigest(),
                  dispatch_cases=dispatch_cases, normal_completion_cases=matching_cases,
                  empty_submit_packets=64, invalid_or_noncompletion_rejections=rejects,
                  shared_pause_gate_cases=pause_cases,
                  backpressure_and_budget_passed=True, hardware_accessed=False,
                  limits='RAM instruction comparison; preemption/fault completion and GPU execution not implemented or verified')
    (ROOT / 'reports/firmware-event-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
