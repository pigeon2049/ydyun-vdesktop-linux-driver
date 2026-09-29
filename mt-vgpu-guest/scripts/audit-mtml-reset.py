#!/usr/bin/env python3
"""Resolve reference MTML reset vtables and execute its terminal error stub.

Original instructions run in Unicorn RAM only; no Windows imports or device I/O.
"""
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RSP, UC_X86_REG_RAX, UC_X86_REG_RIP

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = '26ec00ff915d369aca514e6b92d94a42ef4ab6e049e9deec33ed2f97bbc29071'


def main():
    data = Path('/opt/MTT-driver-only/mtml.dll').read_bytes()
    assert hashlib.sha256(data).hexdigest() == EXPECTED
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    base = struct.unpack_from('<Q', data, pe + 48)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    sections = []
    for i in range(count):
        _, rva, length, raw = struct.unpack_from('<4I', data, pe + 24 + optional + 40 * i + 8)
        sections.append((base + rva, length, raw))

    def address(offset):
        for va, length, raw in sections:
            if raw <= offset < raw + length:
                return va + offset - raw
        raise ValueError(offset)

    def read(va, length):
        for start, n, raw in sections:
            if start <= va and va + length <= start + n:
                return data[raw + va - start:raw + va - start + length]
        raise ValueError(hex(va))

    def vtable(name):
        td = address(data.index(('.?AV' + name + '@mtmp@@').encode()) - 16)
        needle = struct.pack('<I', td - base)
        cursor = 0
        while True:
            hit = data.find(needle, cursor)
            if hit < 0:
                raise ValueError(name)
            cursor = hit + 1
            if hit < 12 or struct.unpack_from('<I', data, hit - 12)[0] != 1:
                continue
            col = address(hit - 12)
            if struct.unpack_from('<I', data, hit + 8)[0] != col - base:
                continue
            pointer = data.find(struct.pack('<Q', col))
            if pointer >= 0:
                return address(pointer + 8)

    tables = []
    for name, slot, expected in [('GuestDevice', 0xb8, 0x18007e640)] + [
        (n, 0x60, 0x1800afc60) for n in
        ('GpuFeatObj', 'SudiGpuFeatObj', 'Qy1GpuFeatObj', 'Qy2GpuFeatObj', 'Ph1GpuFeatObj')]:
        vt = vtable(name)
        target = struct.unpack('<Q', read(vt + slot, 8))[0]
        assert target == expected
        tables.append({'class': name, 'vtable': hex(vt), 'slot': hex(slot), 'target': hex(target)})

    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    end = max(va + length for va, length, _ in sections)
    uc.mem_map(base, (end - base + 4095) & ~4095)
    for va, length, raw in sections:
        uc.mem_write(va, data[raw:raw + length])
    uc.mem_map(0x200000, 0x100000)
    allowed = [(0x18008aaa0, 0x18008aad4), (0x18004bd20, 0x18004bd8c)]

    def instruction(machine, addr, size, opaque):
        assert any(a <= addr < b for a, b in allowed), hex(addr)

    uc.hook_add(UC_HOOK_CODE, instruction)

    def run(entry, arg1, arg2=0):
        uc.mem_write(0x2f0000, struct.pack('<Q', 0x2ff000))
        for reg, value in ((UC_X86_REG_RSP, 0x2f0000), (UC_X86_REG_RCX, arg1), (UC_X86_REG_RDX, arg2)):
            uc.reg_write(reg, value)
        uc.emu_start(entry, 0x2ff000, count=1000)
        assert uc.reg_read(UC_X86_REG_RIP) == 0x2ff000
        return uc.reg_read(UC_X86_REG_RAX)

    cases = []
    for ready, internal, public in ((0, 5, 2), (1, 6, 4)):
        uc.mem_write(0x200038, bytes([ready]))
        uc.mem_write(0x201000, bytes(24))
        assert run(0x18008aaa0, 0x200000, 0x201000) == 0x201000
        value = struct.unpack('<Q', uc.mem_read(0x201008, 8))[0]
        assert value == internal and run(0x18004bd20, value) == public
        cases.append({'backend_ready': ready, 'internal_error': internal, 'public_error': public})
    report = {'reference_sha256': EXPECTED, 'vtable_slots': tables, 'terminal_stub': '18008aaa0',
              'terminal_stub_cases': cases, 'public_error_4': 'Not Supported (mtmlErrorString)',
              'device_accessed': False, 'reset_performed': False,
              'scope': 'This fixed Windows MTML build; no claim about other releases or all kernel reset paths'}
    (ROOT / 'reports/mtml-reset-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
