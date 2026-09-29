#!/usr/bin/env python3
"""Offline audit of r29: kernel receipts plus independently captured GPU bytes."""
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
SAVED = ROOT / 'build/r29-live'
CASES = [(256,0,0),(1,0,0),(15,0,0),(16,0,0),(31,0,0),
         (257,0,0),(1024,0,0),(4096,0,0),(255,1,3),(257,3,17),
         (1023,17,33),(2048,64,128),(7,4089,4089),(16,4080,4077),(3000,111,113)]


def pairs(image):
    base = 0x6ddd0 + 0x2e30 + 0x2e00
    return [[struct.unpack_from('<I', image, base + i * 16 + j)[0]
             for j in (0, 8)] for i in range(3)]


def main():
    status = json.loads((SAVED / 'repeat-status.json').read_text())
    assert status['result'] == '0' and status['passed'] == status['submitted'] == '80'
    assert status['sequence'] == '82' and status['roots_unchanged'] == 'Y'
    pattern = (r'mt_live_tqx_repeat: case=(\d+) bytes=(\d+) src=(\d+) dst=(\d+) '
               r'sequence=(\d+) result=(-?\d+) passed=(\d+)')
    records = [tuple(map(int, x)) for x in re.findall(pattern, (SAVED / 'dmesg.log').read_text())]
    assert len(records) == 80
    for i, record in enumerate(records):
        assert record == (i, *CASES[i % len(CASES)], i + 3, 0, i + 1), record
    memory = (SAVED / 'tqx-memory.bin').read_bytes()
    assert len(memory) == 0x26000
    root, src, dst = memory[:0x20000], memory[0x21000:0x22000], memory[0x22000:0x23000]
    before_root = (ROOT / 'build/r28-live/tqx-after-root.bin').read_bytes()
    assert root == before_root
    i = 79
    expected_src = bytes(((j * 73 + 19 + i * 37) ^ (j >> 3) ^ (i >> 2)) & 255 for j in range(4096))
    count, so, do = CASES[i % len(CASES)]
    expected_dst = bytearray([(0xa5 ^ (i * 13)) & 255] * 4096)
    expected_dst[do:do+count] = expected_src[so:so+count]
    assert src == expected_src
    assert dst == expected_dst
    before_fw = (ROOT / 'build/r28-live/tqx-after-trial_firmware').read_bytes()
    after_fw = (SAVED / 'after-trial_firmware').read_bytes()
    before_pairs, after_pairs = pairs(before_fw), pairs(after_fw)
    assert before_pairs == [[2, 2], [0, 0], [2, 2]]
    assert after_pairs == [[18, 18], [0, 0], [18, 18]]
    assert (2 + len(records)) % 64 == 18
    runtime = (SAVED / 'after-runtime').read_text().strip()
    assert 'guest=2 firmware=2 started=1 event_result=0' in runtime
    completion = (SAVED / 'after-completions').read_text().strip()
    assert completion == 'pending=0 completed=82 submit_enabled=0 workload_submit=0'
    report = {
        'hardware_access_by_verifier': False,
        'boot_id': 'fe18deeb-d431-4c03-ac88-34235e9849c5',
        'gpu_copies_passed': len(records), 'distinct_cases': len(CASES),
        'cases': [{'bytes': b, 'source_offset': s, 'destination_offset': d} for b,s,d in CASES],
        'fence_first': 3, 'fence_last': 82,
        'dm1_ring_pairs_before': before_pairs, 'dm1_ring_pairs_after': after_pairs,
        'ring_wrap_verified': True,
        'root_matches_r28': root == before_root,
        'root_sha256': hashlib.sha256(root).hexdigest(),
        'final_source_all_4096_bytes_verified': src == expected_src,
        'final_destination_and_guards_verified': dst == expected_dst,
        'final_source_sha256': hashlib.sha256(src).hexdigest(),
        'final_destination_sha256': hashlib.sha256(dst).hexdigest(),
        'snapshot_sha256': hashlib.sha256(memory).hexdigest(),
        'helper_sha256': hashlib.sha256((SAVED / 'mt_live_tqx_repeat.ko').read_bytes()).hexdigest(),
        'runtime': runtime, 'completions': completion,
        'limits': '80 sequential fixed-VM GPU copies. Every case checked in kernel; only final data and page tables independently snapshotted. No concurrency, root withdrawal, DRM/UMD or graphics acceleration claim.',
    }
    (ROOT / 'reports/r29-copy-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
