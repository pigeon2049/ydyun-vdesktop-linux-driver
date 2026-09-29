#!/usr/bin/env python3
"""Compare queue memory and kick arguments against Windows x86 instructions.

No device or imported Windows OS code executes. MMIO is intercepted at the
reference write helper and recorded, not performed. Memory ordering is checked
as backend call order; cache/PCI ordering still needs live firmware validation.
"""
import ctypes
import errno
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
SIZE, STRIDE = 0x11520, 0x2e30
QUEUE, CONTEXT, COMMAND, PARAMETERS = 0x400000, 0x220000, 0x221000, 0x222000


def main():
    out = ROOT / 'build/firmware'
    out.mkdir(parents=True, exist_ok=True)
    library = out / 'queue.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', '-fPIC', str(ROOT / 'tests/fw_queue_oracle_wrapper.c'),
                    '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    pointer, uint = ctypes.c_void_p, ctypes.c_uint32
    lib.submit.argtypes = [pointer, uint, uint, uint, pointer, pointer]
    lib.idle.argtypes = [pointer, uint, uint]
    lib.work_idle.argtypes = [pointer, uint]
    lib.command.argtypes = [pointer, uint, uint, pointer]
    lib.initialize.argtypes = [pointer, uint]
    oracle = ReferenceOracle()
    oracle.import_noop(0x1401380c0, 0x2f8000)  # Single-threaded producer lock.
    oracle.import_noop(0x1401380c8, 0x2f8010)
    oracle.put64(0x140138000, 0x2f8020)  # Count stalled retries without sleeping.
    stalls = []
    oracle.hooks[0x2f8020] = lambda: (stalls.append(oracle.arg(0)), oracle.ret())
    oracle.put64(0x1401380f0, 0x2f8030)  # Deterministic process ID.
    pid = 0
    oracle.hooks[0x2f8030] = lambda: oracle.ret(pid)
    kicks = []

    def capture_kick():
        # Third argument is a byte; Windows leaves the upper register bits unspecified.
        kicks.append((oracle.arg(0), oracle.arg(1) & 0xffffffff,
                      oracle.arg(2) & 255, oracle.arg(3) & 0xffffffff,
                      oracle.arg(4) & 0xffffffff))
        oracle.ret()

    oracle.hooks[0x14001b750] = capture_kick
    oracle.put64(CONTEXT + 0x20, QUEUE)
    oracle.uc.mem_write(CONTEXT + 0x49, b'\x04')
    rng = random.Random(275)
    baseline = rng.randbytes(SIZE)
    candidate = ctypes.create_string_buffer(SIZE)
    command = ctypes.create_string_buffer(80)
    trace = (uint * 65)()
    enqueue_ranges = [(0x14000bf20, 0x14000c0ab)]
    enqueue_count = full_count = idle_count = invalid_count = 0

    for dm in range(6):
        for ring in range(2):
            cursor = dm * STRIDE + 0x2e00 + ring * 16
            for head in range(64):
                # Empty and nonempty rings; every position including wrap.
                tail = (head - rng.randrange(63)) & 63
                data = bytearray(baseline)
                struct.pack_into('<I', data, cursor, head)
                struct.pack_into('<I', data, cursor + 8, tail)
                payload = rng.randbytes(80)
                oracle.uc.mem_write(QUEUE, bytes(data))
                oracle.uc.mem_write(COMMAND, payload)
                kicks.clear()
                oracle.run(0x14000bf20, [0, 0x12345, CONTEXT, dm, COMMAND, ring], enqueue_ranges)
                expected = bytes(oracle.uc.mem_read(QUEUE, SIZE))
                assert kicks == [(0x12345, 0, 4, 0xb00, dm)]
                ctypes.memmove(candidate, bytes(data), SIZE)
                ctypes.memmove(command, payload, 80)
                trace[0] = 0
                assert lib.submit(candidate, SIZE, dm, ring, command, trace) == 0
                assert candidate.raw == expected
                destination = dm * STRIDE + ring * 0x1400 + head * 80
                assert list(trace[1:trace[0] + 1]) == [
                    0x10000000 | cursor, 0x10000000 | (cursor + 8),
                    0x20000000 | destination, 0x30000000, 0x40000000 | cursor,
                    0x30000000, 0x10000000 | cursor, 0x50000000 | dm, 0x30000000]
                enqueue_count += 1
            # Reference silently times out after 10,000 stalls; Linux returns
            # EAGAIN immediately. Both leave all bytes untouched and do not kick.
            struct.pack_into('<I', data, cursor + 8, (head + 1) & 63)
            oracle.uc.mem_write(QUEUE, bytes(data))
            kicks.clear()
            stalls.clear()
            oracle.run(0x14000bf20, [0, 0x12345, CONTEXT, dm, COMMAND, ring], enqueue_ranges)
            assert len(stalls) == 10000 and set(stalls) == {90} and not kicks
            assert bytes(oracle.uc.mem_read(QUEUE, SIZE)) == data
            ctypes.memmove(candidate, bytes(data), SIZE)
            trace[0] = 0
            assert lib.submit(candidate, SIZE, dm, ring, command, trace) == -errno.EAGAIN
            assert candidate.raw == data and trace[0] == 2
            full_count += 1

    # Compare all 18 ring cursor pairs in idle, command-pending and
    # event-pending states, including DM0 exclusion from the work-idle check.
    for dm in range(6):
        for ring in range(3):
            for pending in (0, 1):
                data = bytearray(SIZE)
                for d in range(6):
                    for r in range(3):
                        value = rng.randrange(64)
                        struct.pack_into('<I4xI', data, d * STRIDE + 0x2e00 + r * 16, value, value)
                cursor = dm * STRIDE + 0x2e00 + ring * 16
                if pending:
                    head = struct.unpack_from('<I', data, cursor)[0]
                    struct.pack_into('<I', data, cursor, (head + 1) & 63)
                oracle.uc.mem_write(QUEUE, bytes(data))
                ctypes.memmove(candidate, bytes(data), SIZE)
                expected = oracle.run(0x14000bdc4, [QUEUE + dm * STRIDE],
                                      [(0x14000bdc4, 0x14000be03)]) & 255
                assert lib.idle(candidate, SIZE, dm) == expected
                expected = oracle.run(0x14000bc9c, [QUEUE],
                                      [(0x14000bc9c, 0x14000bce8),
                                       (0x14000bdc4, 0x14000be03)]) & 255
                assert lib.work_idle(candidate, SIZE) == expected
                idle_count += 1

    captured = []

    def capture_command():
        assert (oracle.arg(0), oracle.arg(1), oracle.arg(2), oracle.arg(3), oracle.arg(5)) == (
            0, 0x12345, CONTEXT, 0, 0)
        captured.append(bytes(oracle.uc.mem_read(oracle.arg(4), 80)))
        oracle.ret()

    oracle.hooks[0x14000bf20] = capture_command
    for i in range(66):
        opcode = (0x46, 0x47)[i] if i < 2 else rng.getrandbits(32)
        pid = rng.getrandbits(32)
        params = (uint * 3)(*(rng.getrandbits(32) for _ in range(3))) if i % 2 else None
        if params is not None:
            oracle.uc.mem_write(PARAMETERS, bytes(params))
        captured.clear()
        oracle.run(0x14000c0ac, [0x12345, CONTEXT, 0, opcode, PARAMETERS if params is not None else 0],
                   [(0x14000c0ac, 0x14000c150)])
        lib.command(command, opcode, pid, params)
        assert captured == [command.raw]

    # Initialization must affect exactly the known queue range.
    oracle.uc.mem_write(QUEUE, baseline + b'Z' * 32)
    oracle.run(0x14000c198, [QUEUE], [(0x14000c198, 0x14000c1a5)])
    guarded = ctypes.create_string_buffer(baseline + b'Z' * 32, SIZE + 32)
    assert lib.initialize(guarded, SIZE) == 0
    assert guarded.raw == bytes(oracle.uc.mem_read(QUEUE, SIZE + 32)) == bytes(SIZE) + b'Z' * 32

    # Reject indices/sizes and corrupted cursors instead of replicating the
    # reference's unchecked out-of-range accesses. No writes or kicks allowed.
    for dm, ring, size in [(6, 0, SIZE), (0xffffffff, 0, SIZE), (0, 2, SIZE),
                           (0, 0xffffffff, SIZE), (0, 0, SIZE - 1)]:
        ctypes.memmove(candidate, baseline, SIZE)
        trace[0] = 0
        assert lib.submit(candidate, size, dm, ring, command, trace) == -errno.EINVAL
        assert trace[0] == 0 and candidate.raw == baseline
        invalid_count += 1
    for offset in (0x2e00, 0x2e08):
        for bad in (64, 0xffffffff):
            data = bytearray(SIZE)
            struct.pack_into('<I', data, offset, bad)
            ctypes.memmove(candidate, bytes(data), SIZE)
            trace[0] = 0
            assert lib.submit(candidate, SIZE, 0, 0, command, trace) == -errno.EIO
            assert candidate.raw == data and trace[0] == 2
            assert lib.idle(candidate, SIZE, 0) == -errno.EIO
            invalid_count += 1
    assert lib.initialize(candidate, SIZE - 1) == -errno.EINVAL
    assert candidate.raw == data
    assert lib.idle(candidate, SIZE, 6) == -errno.EINVAL
    assert lib.work_idle(candidate, SIZE - 1) == -errno.EINVAL
    report = {
        'reference_sha256': hashlib.sha256(oracle.pe.data).hexdigest(),
        'hardware_written': False, 'queue_bytes': SIZE, 'dm_count': 6,
        'command_bytes': 80, 'event_bytes': 24, 'ring_entries': 64,
        'usable_entries_per_ring': 63, 'enqueue_cases_passed': enqueue_count,
        'full_ring_cases_passed': full_count, 'idle_cases_passed': idle_count,
        'command_cases_passed': 66, 'invalid_enqueue_inputs_rejected': invalid_count,
        'guarded_initialization_matches': True, 'backend_order_verified': True,
        'device_ordering_verified': False, 'firmware_connection_verified': False,
        'modeled_helpers': ['CPU memset', 'single-threaded producer spinlock',
                            'stall counter', 'process ID', 'captured MMIO write helper'],
    }
    (ROOT / 'reports/firmware-queue-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
