#!/usr/bin/env python3
"""Compare a read-only six-range snapshot with the retained trial's upload data.

Also walk every firmware page through the captured GPU page tables in software.
No device access; this cannot prove Host mappings or GPU execution.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'build/firmware'
SIZES = [0x800000, 0x6000, 0x1000, 0x3000, 0x80000, 0x80000]
NAMES = ['firmware', 'tables', 'auxiliary', 'dummy', 'yuv', 'dm_kill']


def digest(data):
    return hashlib.sha256(data).hexdigest()


def split(data):
    assert len(data) == sum(SIZES)
    cursor = 0
    result = []
    for size in SIZES:
        result.append(data[cursor:cursor + size])
        cursor += size
    return result


def main():
    source = ROOT / 'build/recovery-channel/retained-six-ranges.bin'
    actual = split(source.read_bytes())
    backup = split((FW / 'trial_backups-first-trial.bin').read_bytes())
    stage = (FW / 'bootstrap-stage-live.bin').read_bytes()
    assert len(stage) == 0x109000
    # Static resource writes preserve padding, unlike zero-based examples.
    yuv, kill = bytearray(backup[4]), bytearray(backup[5])
    yuv_constants = (FW / 'yuv-initial.bin').read_bytes()
    kill_constants = (FW / 'dm-kill-initial.bin').read_bytes()
    for offset in [64 * i for i in range(12)] + [0x3c0]:
        yuv[offset:offset + 24] = yuv_constants[offset:offset + 24]
    kill[:68] = kill_constants[:68]
    expected = [(FW / 'trial-after-package.bin').read_bytes(), stage[:0x6000],
                b'\xba' * 4096, stage[0x6000:0x9000], bytes(yuv), bytes(kill)]
    records = []
    for name, size, before, after in zip(NAMES, SIZES, expected, actual):
        assert len(before) == size and len(after) == size
        differences = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        records.append({'name': name, 'bytes': size, 'actual_sha256': digest(after),
                        'expected_sha256': digest(before), 'equal': not differences,
                        'different_bytes': len(differences),
                        'first_different_offsets': differences[:16]})
    tables = actual[1]
    table_pa, fw_pa, fw_va = 0x605800000, 0x771fef000, 0xe1c0000000

    def read_table(pa, index, width):
        offset = pa - table_pa + width * index
        assert pa & 4095 == 0 and 0 <= offset <= len(tables) - width
        return struct.unpack_from('<I' if width == 4 else '<Q', tables, offset)[0]

    failures = []
    for i in range(2048):
        va = fw_va + i * 4096
        try:
            pc = read_table(table_pa, (va >> 30) & 1023, 4)
            assert pc & 1
            pd = read_table((pc & ~15) << 8, (va >> 21) & 511, 8)
            assert pd & 1
            pt = read_table(pd & 0xffffffffe0, (va >> 12) & 511, 8)
            assert pt & 1 and pt & (1 << 62)
            assert pt & 0xfffffff000 == fw_pa + i * 4096
        except (AssertionError, struct.error):
            failures.append(hex(va))
    report = {'snapshot': str(source.relative_to(ROOT)), 'snapshot_sha256': digest(source.read_bytes()),
              'ranges': records, 'software_walk': {'pages': 2048, 'failed_pages': failures,
              'root_gpu_pa': hex(table_pa), 'firmware_va': hex(fw_va), 'firmware_gpu_pa': hex(fw_pa)},
              'all_ranges_match': all(r['equal'] for r in records),
              'gpu_execution_verified': False, 'host_mapping_verified': False}
    (ROOT / 'reports/retained-memory-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if failures or not report['all_ranges_match']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
