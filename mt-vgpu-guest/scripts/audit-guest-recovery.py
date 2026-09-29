#!/usr/bin/env python3
"""Audit the reference Guest HWR request in emulated RAM; never access a device.

Host assembly is evidence from 2.3, not the running Host's implementation.
This script does not implement or issue a hardware recovery request.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
DEVICE, CONFIG, GPU, SHARED = 0x200000, 0x204000, 0x208000, 0x20c000


def main():
    o = ReferenceOracle()
    o.put64(DEVICE, CONFIG)
    o.put64(DEVICE + 0x418, GPU)
    o.put64(GPU + 0x1cd0, SHARED)
    o.uc.mem_write(CONFIG, b'\1')
    writes = []

    def write():
        assert o.arg(0) == GPU + 8
        writes.append({'offset': hex(o.arg(1)), 'value': o.arg(2)})
        o.ret()

    o.hooks[0x140027780] = write
    ranges = [(0x1400088c0, 0x140008922), (0x14000e494, 0x14000e4d8),
              (0x140023904, 0x14002390b), (0x140023958, 0x14002398b),
              (0x140022f24, 0x140022f38)]
    cases = []
    for flag in (0, 1):
        for status in (0, 1, 2, 3):
            before = bytes([0xa5]) * 4096
            o.uc.mem_write(SHARED, before)
            o.put32(GPU + 0x1db8, status)
            o.uc.mem_write(GPU + 0x1dbc, bytes([1, flag]))
            writes.clear()
            assert o.run(0x1400088c0, [DEVICE], ranges) & 0xffffffff == 0
            cleared = bool(flag and status == 2)
            expected = bytearray(before)
            if cleared:
                expected[0x10:0x20] = bytes(16)
            assert bytes(o.uc.mem_read(SHARED, 4096)) == expected
            assert o.get64(GPU + 0x1db8) & 0xffffffffffff == (
                0 if cleared else status | (1 << 32) | (flag << 40))
            assert writes == [{'offset': '0x118', 'value': 1}]
            cases.append({'local_flag': flag, 'local_state': status,
                          'cleared_shared_10_18': cleared, 'writes': list(writes)})

    # Inventory every direct decompiler call to the BAR1 64-bit wrapper.
    # Does not claim to enumerate indirect calls or other MMIO primitives.
    corpus = ROOT / 'decompiled/mtkm64.sys'
    lines = (corpus / 'decompiled.c').read_text().splitlines()
    inventory = []
    for row in (json.loads(s) for s in (corpus / 'functions.jsonl').read_text().splitlines()):
        if 'c_line_start' not in row or row['address'] == '140027780':
            continue
        begin = row['c_line_start'] - 1
        for number, line in enumerate(lines[begin:begin + row['c_line_count']], begin + 1):
            if re.search(r'\bFUN_140027780\(', line):
                inventory.append({'caller': row['address'], 'line': number, 'call': line.strip()})
    assert any(x['caller'] == '140022f24' for x in inventory)
    host = ROOT / 'src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/objs/x86_64/mtgpu_core.o_binary'
    report = {
        'utc': datetime.now(timezone.utc).isoformat(), 'passed': True,
        'reference_sha256': o.pe.sha256, 'device_accessed': False,
        'hwr_request_entry': '1400088c0', 'original_instruction_cases': cases,
        'bar1_direct_call_inventory': inventory,
        'old_host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
        'old_host_static_path': [
            '54140 BAR1 handler: +0x118/value=1 -> 54db6 vgpu_hwr_request',
            '53f90 vgpu_hwr_request: is_gpu_normal; on abnormal, schedule MPC work',
            '4a3a0 vgpu_mpc_hwr_work: abnormal branch -> reset_gpu at 4a3e3'],
        'current_host_semantics_verified': False,
        'hardware_request_issued': False,
        'reason_not_issued': 'Old Host path can reset an MPC; scope is not proven limited to this VM.',
        'limitations': 'CPU oracle verifies Guest writes/cleanup only. It does not emulate the Host, reset a GPU, or establish acceleration.',
    }
    (ROOT / 'reports/guest-recovery-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} Guest HWR request cases; {len(inventory)} direct BAR1 calls; no device access')


if __name__ == '__main__':
    main()
