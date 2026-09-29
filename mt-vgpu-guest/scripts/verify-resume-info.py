#!/usr/bin/env python3
"""Execute the reference resume-info wrapper; model Host writes and replies.

Proves the OSID value, call order and layout comparison, not Host semantics.
"""
import json
from pathlib import Path
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
PLATFORM, INFO, BACKUP = 0x200000, 0x204000, 0x205000


def main():
    original = (ROOT / 'reports/device-info.bin').read_bytes()[:0xcc8]
    o = ReferenceOracle()
    o.put64(PLATFORM + 0x1cb0, INFO)
    o.put64(PLATFORM + 0x1cb8, BACKUP)
    events = []
    response = original

    def publish():
        assert o.arg(0) == PLATFORM
        events.append({'offset': o.arg(1), 'value': o.arg(2)})
        o.ret()

    def query():
        assert o.arg(0) == PLATFORM
        assert events == [{'offset': 0x110, 'value': 4}]
        assert bytes(o.uc.mem_read(BACKUP, 0xcc8)) == original
        events.append({'query_info': True})
        o.uc.mem_write(INFO, response)
        o.ret()

    o.hooks[0x140027780] = publish
    o.hooks[0x140026b30] = query
    cases = []
    for offset in (None, 0x18, 0x20, 0xc50, 0x28, 0x30, 0x38):
        response = bytearray(original)
        if offset is not None:
            response[offset] ^= 1
        response = bytes(response)
        events.clear()
        o.uc.mem_write(INFO, original)
        o.uc.mem_write(BACKUP, bytes(0xcc8))
        o.uc.mem_write(PLATFORM + 0x1cd0, b'\xa5')
        assert o.run(0x1400270e0, [PLATFORM], [(0x1400270e0, 0x1400271d7)]) == 0
        changed = o.uc.mem_read(PLATFORM + 0x1cd0, 1)[0]
        assert changed == (offset is not None)
        cases.append({'changed_offset': offset, 'layout_changed': changed,
                      'events': list(events)})
    report = {'reference_entry': '1400270e0', 'cases': cases, 'passed': True,
              'host_semantics_verified': False, 'device_accessed': False}
    (ROOT / 'reports/resume-info-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 7 reference resume-info cases; no device access')


if __name__ == '__main__':
    main()
