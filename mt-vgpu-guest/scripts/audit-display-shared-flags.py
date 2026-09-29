#!/usr/bin/env python3
"""Execute shared-page display flag leaves in emulated RAM, never MMIO."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
ADAPTER, DEVICE, GPU, PAGE, TABLE = (0x200000, 0x202000, 0x204000, 0x208000, 0x20a000)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true', help='Read current shared page through sysfs')
    args = parser.parse_args()
    o = ReferenceOracle()
    o.put64(ADAPTER + 0x248, DEVICE)
    o.put64(DEVICE + 0x418, GPU)
    o.put64(GPU + 0x1cd0, PAGE)
    o.run(0x140003eb0, [TABLE], [(0x140003eb0, 0x14000405c)])
    entries = [o.get64(TABLE + x) for x in (0xf8, 0x100, 0x108)]
    assert entries == [0x1400066f8, 0x1400066e4, 0x14000645c]
    ranges = [(0x1400066f8, 0x14000670b), (0x1400066e4, 0x1400066f7),
              (0x14000645c, 0x14000646f), (0x140022430, 0x140022462),
              (0x140022280, 0x1400222b2), (0x140021ee0, 0x140021f30)]
    cases = []
    for channel in range(4):
        enable_bit, pending_bit = 1 << (channel * 8), 2 << (channel * 8)
        for before in (0, 0xffffffff, 0x01010101, 0x02020202, 0xa5a55a5a):
            for operation, entry in zip(('enable', 'disable', 'test-and-clear'), entries):
                sentinel = bytearray(b'\xa5' * 4096)
                struct.pack_into('<I', sentinel, 0x328, before)
                o.uc.mem_write(PAGE, bytes(sentinel))
                result = o.run(entry, [ADAPTER, channel], ranges) & 255
                expected = (before | enable_bit) if operation == 'enable' else (
                    before & ~enable_bit if operation == 'disable' else before & ~pending_bit)
                struct.pack_into('<I', sentinel, 0x328, expected)
                assert bytes(o.uc.mem_read(PAGE, 4096)) == bytes(sentinel)
                if operation == 'test-and-clear':
                    assert result == bool(before & pending_bit)
                cases.append(dict(channel=channel, operation=operation,
                                  before=hex(before), after=hex(expected),
                                  boolean_result=result if operation == 'test-and-clear' else None))
    for missing in ('gpu', 'page'):
        o.put64(DEVICE + 0x418, 0 if missing == 'gpu' else GPU)
        o.put64(GPU + 0x1cd0, 0 if missing == 'page' else PAGE)
        before = bytes(o.uc.mem_read(PAGE, 4096))
        for entry in entries:
            result = o.run(entry, [ADAPTER, 0], ranges) & 255
            if entry == entries[2]:
                assert result == 0
            assert bytes(o.uc.mem_read(PAGE, 4096)) == before
    report = dict(reference_sha256=o.pe.sha256,
                  display_reference_sha256=hashlib.sha256(Path('/opt/MTT-driver-only/mtdispkm64.sys').read_bytes()).hexdigest(),
                  passed=True, cases=cases, null_pointer_cases=6,
                  table_offsets=['0xf8', '0x100', '0x108'],
                  hardware_written=False, host_emulated=False,
                  limits='Leaf behavior and callback-table identity only; display caller and startup ordering are separately inspected static evidence. Valid display channel count is not inferred from x86 shift wrapping.')
    if args.live:
        base = '/sys/bus/pci/devices/0000:00:0e.0/mt_guest/'
        raw = subprocess.run(['sudo', '-n', 'cat', base + 'channels_raw'], check=True, capture_output=True).stdout
        assert len(raw) == 4 * 4096
        report['live_read_only'] = {'page0_328':hex(struct.unpack_from('<I', raw, 0x328)[0])}
        for attr in ('connection', 'rpc_service', 'retained_status'):
            report['live_read_only'][attr] = subprocess.run(['sudo', '-n', 'cat', base + attr], check=True, capture_output=True, text=True).stdout.strip()
    (ROOT / 'reports/display-shared-flags-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} display flag cases plus 6 null guards; no device writes')
    if args.live:
        print('Current page0+0x328 = ' + report['live_read_only']['page0_328'])


if __name__ == '__main__':
    main()
