#!/usr/bin/env python3
"""Run the original Guest PCI enable suffix with a recorded config callback."""
import json
import struct
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_R14, UC_X86_REG_R12, UC_X86_REG_RAX
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
ADAPTER, CONFIG, DISPATCH, CALLBACK = 0x200000, 0x202000, 0x204000, 0x205000


def main():
    o = ReferenceOracle()
    cases = []
    for guest in (0, 1):
        for command in (0, 3, 7, 0x403):
            for outcome in ('success', 'error', 'short'):
                o.uc.mem_write(ADAPTER, bytes(4096))
                o.uc.mem_write(CONFIG, bytes(256))
                o.uc.mem_write(CONFIG + 4, struct.pack('<HH', command, 0x10))
                o.uc.mem_write(ADAPTER + 0x8c4, bytes([guest]))
                o.put64(ADAPTER + 0x8b0, CONFIG)
                o.put64(ADAPTER + 0x268, 0xabcdef)
                o.put64(ADAPTER + 0x2b8, CALLBACK)
                o.put64(0x140138420, DISPATCH)
                writes = []

                def dispatch():
                    assert o.uc.reg_read(UC_X86_REG_RAX) == CALLBACK
                    assert [o.arg(i) for i in range(5)] == [0xabcdef, 0, CONFIG + 4, 4, 4]
                    writes.append(bytes(o.uc.mem_read(o.arg(2), 4)).hex())
                    first = len(writes) == 1
                    o.put32(o.arg(5), 2 if first and outcome == 'short' else 4)
                    o.ret(0xc0000001 if first and outcome == 'error' else 0)

                o.hooks[DISPATCH] = dispatch
                # Enter after setup; stop before epilogue whose stack belongs to
                # the full function. r12=4 and r14=&adapter->DeviceHandle.
                o.hooks[0x140003e18] = o.ret
                o.uc.reg_write(UC_X86_REG_RDI, ADAPTER)
                o.uc.reg_write(UC_X86_REG_R14, ADAPTER + 0x268)
                o.uc.reg_write(UC_X86_REG_R12, 4)
                o.run(0x140003d94, [], [(0x140003d94, 0x140003e18)])
                expected = command | 6 if guest else command
                assert struct.unpack('<HH', o.uc.mem_read(CONFIG + 4, 4)) == (expected, 0x10)
                assert len(writes) == (0 if not guest else 1 if outcome == 'success' else 2)
                assert all(w == struct.pack('<HH', expected, 0x10).hex() for w in writes)
                cases.append(dict(guest=guest, command_before=hex(command),
                                  command_after=hex(expected), first_result=outcome,
                                  config_writes=writes))
    report = dict(reference_sha256=o.pe.sha256, entry='140003af4',
                  executed_range=['140003d94', '140003e18'], passed=True, cases=cases,
                  hardware_written=False,
                  scope='Original PCI enable suffix; callbacks recorded only. No proof that BusMaster explains the firmware stall.')
    (ROOT / 'reports/pci-enable-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} original PCI enable cases; no hardware access')


if __name__ == '__main__':
    main()
