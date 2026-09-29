#!/usr/bin/env python3
"""Execute recovered transport leaves in emulated RAM, with MMIO recorded only."""
import json
from pathlib import Path
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
PLATFORM, PAGE, OUT = 0x200000, 0x204000, 0x206000


def main():
    o = ReferenceOracle()
    cases = []
    # The registration callback does not set valid; its caller ORs bit 0.
    for slot in range(4):
        o.put64(OUT, 0)
        o.run(0x140024ee4, [OUT, slot], [(0x140024ee4, 0x140024f13)])
        descriptor = o.get64(OUT) | 1
        assert descriptor == (4096 << 9) | (slot << 1) | 1
        cases.append({'entry': '140024ee4', 'slot': slot, 'descriptor': hex(descriptor)})

    original = bytes([0xa5]) * 4096
    o.uc.mem_write(PAGE, original)
    assert o.run(0x140024f14, [PAGE, PLATFORM], [(0x140024f14, 0x140024f21)]) & 255 == 1
    assert o.get64(PLATFORM + 0x1cc8) == PAGE
    assert bytes(o.uc.mem_read(PAGE, 4096)) == b'\1' + original[1:]
    cases.append({'entry': '140024f14', 'page_bytes_changed': [0], 'gpu_normal': 1})
    o.run(0x140024f24, [PLATFORM], [(0x140024f24, 0x140024f30)])
    assert o.get64(PLATFORM + 0x1cc8) == 0
    assert bytes(o.uc.mem_read(PAGE, 4096)) == b'\1' + original[1:]
    cases.append({'entry': '140024f24', 'only_clears_cpu_pointer': True})

    for slot in range(4):
        value = PAGE + slot * 4096
        o.put64(PLATFORM + (slot + 0xb3) * 0x28, value)
        o.run(0x140027284, [OUT, slot, PLATFORM], [(0x140027284, 0x140027299)])
        assert o.get64(OUT) == value
        cases.append({'entry': '140027284', 'slot': slot, 'cpu_page': hex(value)})

    writes = []
    def write():
        assert o.arg(0) == PLATFORM and o.arg(1) == 0x138
        writes.append(o.arg(2))
        o.ret()
    o.hooks[0x140027780] = write
    for ring in range(4):
        o.run(0x140027350, [ring, PLATFORM], [(0x140027350, 0x140027363)])
        cases.append({'entry': '140027350', 'recorded_bar1_offset': '0x138', 'ring': ring})
    assert writes == [0, 1, 2, 3]
    report = {'reference_sha256': o.pe.sha256, 'passed': True, 'cases': cases,
              'hardware_written': False,
              'scope': 'Recovered transport leaf behavior only; MMIO is recorded, not executed. No GPU recovery or acceleration proof.'}
    (ROOT / 'reports/recovered-callback-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} recovered callback instruction cases; no device access')


if __name__ == '__main__':
    main()
