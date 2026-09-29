#!/usr/bin/env python3
"""RAM-only instruction oracle for full 80-byte submits and logical node routing."""
import ctypes as C
import errno
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
A, MODE, NODE, GROUP, QUEUE = 0x200000, 0x201000, 0x220000, 0x260000, 0x400000
DESC, FLAGS, PLATFORM, TYPES = 0x230000, 0x231000, 0x210000, 0x270000


class Input(C.Structure):
    _fields_ = [(k, C.c_uint64) for k in ('root_pa', 'process_id', 'command_va')] + [
        (k, C.c_uint32) for k in ('type', 'process_pid', 'bytes', 'fence', 'submit_flags')]


def main():
    libpath = ROOT / 'build/firmware/work-command.so'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared', '-fPIC',
                    ROOT / 'tests/work_command_oracle_wrapper.c', '-o', libpath], check=True)
    lib = C.CDLL(str(libpath))
    lib.encode.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
    lib.node.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
    oracle = ReferenceOracle()
    rng = random.Random(275100)
    oracle.put64(A, MODE)
    oracle.uc.mem_write(MODE, b'\x01')
    oracle.put64(A + 0x420, GROUP)
    oracle.put64(GROUP + 0xe0, QUEUE)
    oracle.uc.mem_write(NODE + 0x70, b'\x00')
    oracle.hooks[0x1400238cc] = lambda: oracle.ret(0)  # Unpaused scheduling fixture.
    oracle.hooks[0x14000c92c] = oracle.ret  # Timing history, outside packet scope.
    oracle.hooks[0x140130c50] = oracle.ret  # Stack cookie only.
    packets = []
    dm = 0
    def capture():
        assert oracle.arg(3) == dm and oracle.arg(5) == 0
        packets.append(bytes(oracle.uc.mem_read(oracle.arg(4), 80)))
        oracle.ret()
    oracle.hooks[0x14000bf20] = capture
    packet = C.create_string_buffer(96)
    cases = 0
    for typ in list(range(13)) + [0x80000000, 0xffffffff]:
        for flags in (0, 0x80, 0xffffffff, 0x40, 1, 0x82, 0x80000000, rng.getrandbits(32)):
            # Raw serializer accepts full-width fields. Linux admission performs
            # bounds/type/flag checks separately; a process token is not a PA.
            inp = Input(rng.getrandbits(64), rng.getrandbits(64), rng.getrandbits(64),
                        typ, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), flags)
            desc = bytearray(400)
            struct.pack_into('<I', desc, 8, typ)
            struct.pack_into('<I', desc, 0x1c, inp.process_pid)
            struct.pack_into('<Q', desc, 0x38, inp.command_va)
            struct.pack_into('<Q', desc, 0xc0, inp.root_pa)
            oracle.uc.mem_write(DESC, bytes(desc))
            oracle.put32(FLAGS, flags)
            dm = 1 + cases % 5
            oracle.put32(NODE + 0x74, dm)
            packets.clear()
            oracle.run(0x14001475c, [A, inp.process_id, NODE, DESC, inp.bytes, inp.fence, 0, FLAGS],
                       [(0x14001475c, 0x140014900)])
            C.memset(packet, 0xa5, 96)
            assert lib.encode(packet, 96, C.byref(inp), inp.fence) == 0
            assert packets == [packet.raw[:80]] and packet.raw[80:] == b'\xa5' * 16
            cases += 1
    for wire in (0, 1, 0xffffffff):
        packets.clear()
        oracle.run(0x14001475c, [A, 0xdeadbeef, NODE, 0, 0xffffffff, wire, 0, 0],
                   [(0x14001475c, 0x140014900)])
        assert lib.encode(packet, 80, None, wire) == 0 and packets == [packet.raw[:80]]
    saved = packet.raw
    assert lib.encode(packet, 79, C.byref(inp), 1) == -errno.EINVAL and packet.raw == saved
    assert lib.encode(None, 80, C.byref(inp), 1) == -errno.EINVAL

    # Run the actual node initializer. Pool allocation, timer setup and the
    # security cookie are modeled; the route/capability writes execute directly.
    oracle.put64(A + 0x418, PLATFORM)
    oracle.put64(PLATFORM + 0x9d0, TYPES)
    oracle.put64(0x1401380d0, 0x2f8100)
    oracle.hooks[0x2f8100] = lambda: oracle.ret(oracle.allocate(oracle.arg(1)))
    oracle.hooks[0x14001c410] = lambda: oracle.ret(0)
    oracle.uc.mem_write(MODE + 0x24, b'\x01')  # Skip non-firmware native software-engine setup.
    oracle.put32(MODE + 0x10, 0xffffffff)
    route = (C.c_uint32 * 8)()
    route_cases = 0
    profiles = []
    lists = [list(range(9)), [1, 5, 2, 6, 7], [1, 5, 2, 6, 7, 8]]
    for types in lists:
        oracle.put32(PLATFORM + 0x9d8, len(types))
        oracle.uc.mem_write(TYPES, struct.pack('<' + 'I' * len(types), *types))
        result = oracle.run(0x14000c1e4, [A, 4],
                            [(0x14000c1e4, 0x14000c74c), (0x1400229d4, 0x1400229e5)])
        assert result & 0xffffffff == 0
        base = oracle.get64(A + 0x2d8)
        for index, typ in enumerate(types):
            raw = bytes(oracle.uc.mem_read(base + index * 0x78, 0x78))
            word = lambda off: struct.unpack_from('<I', raw, off)[0]
            expected = (word(8), word(12), word(0x74), word(0x10), word(0x14),
                        word(0x18), word(0x20), word(0x6c))
            assert lib.node(route, typ, index) == 0 and tuple(route) == expected
            assert struct.unpack_from('<H', raw, 0x1c)[0] == route[2]
            route_cases += 1
        profiles.append(dict(types=types, firmware_dm=[{1:1, 2:3, 3:4, 4:5, 5:2, 8:2}.get(t, 6) for t in types]))
    before = bytes(route)
    assert lib.node(route, 9, 0) == -errno.EINVAL and bytes(route) == before
    assert lib.node(None, 1, 0) == -errno.EINVAL
    report = dict(reference_sha256=oracle.pe.sha256, full_packets=cases, empty_packets=3,
                  node_routes=route_cases, profiles=profiles, hardware_accessed=False,
                  limits='Packet/route serialization only; context construction, command stream semantics and execution are not verified')
    (ROOT / 'reports/work-command-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
