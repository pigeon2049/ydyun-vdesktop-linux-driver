#!/usr/bin/env python3
"""Route seven core calls to the v1/v2 dispatcher, preserving its v1 fallback."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

SITES = {
    '_SetupPxE': (0x1e7,),
    'MMU_UnmapPages': (0x66e,),
    'MMU_MapPages': (0x92e,),
    'MMU_MapPMRFast': (0x607,),
    'MMU_AcquireBaseAddr': (0x55,),
    'DevPhysAddr2DmaAddr': (0x84, 0xb6),
}
OLD = 'mtgpu_guest_v1_device_paddr_to_host_device_paddr'
NEW = 'GuestDevicePAddrToHostDevicePAddr'


def patch(path):
    spec = importlib.util.spec_from_file_location('addr_route_helpers',
        Path(__file__).with_name('patch-guest-vz-mmu-bar2-fallback.py'))
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    before = path.read_bytes()
    if b'5c6c275' not in before:
        raise ValueError('unexpected core build')
    sections, named = helper.load_elf_helpers().section_table(before)
    text_index, text = named['.text']
    symbols = helper.find_symbols(before, sections, text_index, {OLD, NEW, *SITES})
    symtab, old_index, _, _ = symbols[OLD]
    if any(s[0] != symtab for s in symbols.values()):
        raise ValueError('symbol tables differ')
    new_index = symbols[NEW][1]
    expected = {symbols[name][2] + offset: (name, offset)
                for name, offsets in SITES.items() for offset in offsets}
    output = bytearray(before)
    changed, fallback = [], []
    for section in sections:
        if section[1] != 4 or section[6] != symtab or section[7] != text_index:
            continue
        for entry in range(section[4], section[4]+section[5], section[9]):
            offset, info, addend = struct.unpack_from('<QQq', before, entry)
            if offset in expected:
                if (info >> 32, info & 0xffffffff, addend) != (old_index, 4, -4):
                    raise ValueError(f'original PLT32 call changed at {offset:#x}')
                if before[text[4]+offset-text[3]-1] != 0xe8:
                    raise ValueError('call opcode changed')
                struct.pack_into('<Q', output, entry+8, (new_index << 32) | 4)
                name, rel = expected[offset]
                changed.append(dict(caller=name, relocation_relative=hex(rel), file_offset=entry+8))
            elif info >> 32 == old_index:
                start, size = symbols[NEW][2:]
                if not (start <= offset < start+size) or (info & 0xffffffff, addend) != (4, -4):
                    raise ValueError('unexpected old-translator reference')
                fallback.append(offset)
    if len(changed) != 7 or len(fallback) != 1:
        raise ValueError('expected seven core calls and one dispatcher fallback')
    allowed = {j for c in changed for j in range(c['file_offset'],c['file_offset']+8)}
    if any(a != b and i not in allowed for i,(a,b) in enumerate(zip(before,output))):
        raise ValueError('unexpected non-relocation edit')
    path.write_bytes(output)
    report = dict(module=str(path.resolve()), sha256=hashlib.sha256(output).hexdigest(),
                  redirected_calls=changed, original_v1_fallback_preserved=True,
                  instruction_bytes_unchanged=True, hardware_accessed=False)
    (path.parent/'guest-vgpu-addr-callers-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: patch-guest-vgpu-addr-callers.py mtgpu.ko')
    print(json.dumps(patch(Path(sys.argv[1])),indent=2))
