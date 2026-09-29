#!/usr/bin/env python3
"""Compare saved r23 device bytes with actual Windows encoder instructions.

No hardware access. RAM allocation placement is modeled as in the live Linux
test; encoding/finalization/DMA serialization execute the reference PE code.
"""
import hashlib
import json
from pathlib import Path
import struct
from tqx_dma_reference import DmaOracle

ROOT = Path(__file__).resolve().parents[1]
SAVED = ROOT / 'build/r23-live'


class LivePlacementOracle(DmaOracle):
    def __init__(self):
        self.cursors = {1: 0, 10: 0}
        super().__init__()

    def allocate_state(self):
        x = self.x
        words, align, va_out, kind, rel_out = [x.arg(i) for i in (1, 2, 3, 4, 5)]
        words &= 0xffffffff
        align &= 0xffffffff
        kind &= 0xffffffff
        assert (kind, words, align) in ((4, 4, 4), (4, 9, 4), (5, 16, 8), (4, 5, 1))
        heap = 10 if kind == 5 else 1
        cursor = (self.cursors[heap] + align * 4 - 1) & ~(align * 4 - 1)
        va = {1: 0x81ffd03000, 10: 0xf0ffe00000}[heap] + cursor
        relative = va - {1: 0x8100000000, 10: 0xf000000000}[heap]
        self.cursors[heap] = cursor + words * 4
        cpu = 0x500000 + len(self.allocations) * 4096
        x.uc.mem_write(cpu, bytes(words * 4))
        x.put64(va_out, va)
        if rel_out:
            x.put64(rel_out, relative)
        self.allocations.append((cpu, words, kind, relative))
        x.ret(cpu)


def main():
    reference = LivePlacementOracle()
    stream = reference.run(0x40100000, 0x40200000, 1, 1, 1,
                           command_va=0x40000000, shader_base=0xfff00000,
                           pds_base=0xffc00000, copy_bytes=256)
    dma = reference.serialize(0x40010000, 0x40020000, 1)
    command_saved = (SAVED / 'tqx-after-command.bin').read_bytes()
    dma_saved = (SAVED / 'tqx-after-dma.bin').read_bytes()
    command_expected = stream['command'].ljust(4096, b'\0')
    dma_expected = dma['data'].ljust(8192, b'\0')
    report = {
        'reference_sha256': reference.x.pe.sha256,
        'command_matches_reference': command_saved == command_expected,
        'dma_matches_reference': dma_saved == dma_expected,
        'command_different_offsets': [i for i, (a, b) in enumerate(zip(command_saved, command_expected)) if a != b],
        'dma_different_offsets': [i for i, (a, b) in enumerate(zip(dma_saved, dma_expected)) if a != b],
        'submission_view': list(struct.unpack('<3Q', dma['view'])),
        'command_sha256': hashlib.sha256(command_saved).hexdigest(),
        'dma_sha256': hashlib.sha256(dma_saved).hexdigest(),
        'hardware_access': False,
        'limits': 'Saved DMA/stream bytes only; allocation placement and platform/OS calls are modeled. Does not validate hardware page permissions, firmware compatibility, engine initialization or GPU execution.',
    }
    (ROOT / 'reports/r23-tqx-reference.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    assert report['command_matches_reference'] and report['dma_matches_reference']


if __name__ == '__main__':
    main()
