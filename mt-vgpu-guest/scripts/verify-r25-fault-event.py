#!/usr/bin/env python3
"""Execute the reference 0x101 handler in RAM; no OS or hardware calls.

The notification and zero-fill helpers are modeled. The handler's guards,
notification stores and context updates execute the original PE instructions.
This is not a GPU recovery test and cannot identify a faulting GPU address.
"""
import itertools
import json
from pathlib import Path
import struct
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
ADAPTER, CTX, NODE, ENTRIES = 0x200000, 0x220000, 0x221000, 0x240000
EVENT, GROUP, ENGINE, NOTIFY = 0x250000, 0x260000, 0x270000, 0x2f8100


def main():
    ref = ReferenceOracle()
    ref.put64(ADAPTER + 0x420, GROUP)
    ref.put64(ADAPTER + 0x1d0, NOTIFY)
    ref.put64(0x140138420, NOTIFY)
    ref.put32(ADAPTER + 0x42c, 64)
    ref.put64(CTX + 0x10, NODE)
    ref.put64(CTX + 0x20, ENTRIES)
    ref.put32(CTX, 3)
    ref.put32(NODE + 0xc, 9)
    notices = []

    def notify():
        notices.append(bytes(ref.uc.mem_read(ref.arg(1), 0x50)))
        ref.ret()

    def zero():
        ref.uc.mem_write(ref.arg(0), bytes(ref.arg(1)))
        ref.ret()

    def u32(address):
        return struct.unpack('<I', ref.uc.mem_read(address, 4))[0]

    ref.hooks[NOTIFY] = notify
    ref.hooks[0x1400083b0] = zero
    cases = accepted_count = 0
    last_notice = None
    for mode, node, head, pending, match in itertools.product(
            (0, 2, 6, 7, 8), (0, 1, 5), (0, 63), (False, True), (False, True)):
        ref.uc.mem_write(NODE + 0x70, bytes([node]))
        ref.put64(GROUP + 0xd8 + node * 0x80, ENGINE)
        ref.put32(ENGINE + 0x20, mode)
        ref.put32(CTX + 0x28, head)
        ref.put32(CTX + 0x2c, (head + int(pending)) & 63)
        ref.put32(CTX + 8, 0x12345678)
        ref.uc.mem_write(CTX + 0x38, b'\0')
        ref.put32(ENTRIES + head * 0x98 + 8, 2 if match else 3)
        # This is the exact r23/r25 fault event, including its fence ID 2.
        ref.uc.mem_write(EVENT, struct.pack('<6I', 0, 0x101, 2, 0, 0, 0))
        before = bytes(ref.uc.mem_read(CTX, 0x40))
        notices.clear()
        ref.run(0x14000e5c8, [ADAPTER, CTX, EVENT], [(0x14000e5c8, 0x14000e6a2)])
        accepted = mode == 7 and pending and match
        assert len(notices) == int(accepted)
        if accepted:
            expected = bytearray(0x50)
            for offset, value in ((0, 9), (8, 2), (0x20, 9), (0x30, 9), (0x34, 3), (0x38, 1)):
                struct.pack_into('<I', expected, offset, value)
            assert notices[0] == expected
            assert u32(CTX + 8) == 2
            assert u32(CTX + 0x28) == (head + 1) & 63
            assert bytes(ref.uc.mem_read(CTX + 0x38, 1)) == b'\1'
            last_notice = notices[0].hex()
            accepted_count += 1
        else:
            assert bytes(ref.uc.mem_read(CTX, 0x40)) == before
        cases += 1
    notices.clear()
    ref.run(0x14000e5c8, [ADAPTER, 0, EVENT], [(0x14000e5c8, 0x14000e6a2)])
    assert not notices
    report = {
        'passed': True, 'reference_sha256': ref.pe.sha256,
        'handler': '0x14000e5c8', 'cases': cases + 1,
        'accepted_cases': accepted_count, 'hardware_access': False,
        'notification_hex': last_notice,
        'notification': {'interrupt_type': 9, 'faulted_fence_id': 2,
                         'page_fault_flags': 9, 'faulted_virtual_address': 0,
                         'node_ordinal': 9, 'engine_ordinal': 3,
                         'page_table_level': 1, 'fault_error_code': 0},
        'limits': 'Notification fields other than fence/node/engine are hardcoded by the reference handler. They do not prove the physical cause or faulting VA. No engine reset, acknowledgement or resource retirement is implemented by this verifier.',
        'sources': [
            'https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_interrupt_type',
            'https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_notify_interrupt_data'],
    }
    (ROOT / 'reports/r25-fault-event.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
