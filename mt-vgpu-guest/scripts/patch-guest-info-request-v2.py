#!/usr/bin/env python3
"""Request the Windows typed info layout at the one audited info-page GPA call."""
import importlib.util
from pathlib import Path
import sys


def main():
    path = Path(__file__).with_name('patch-guest-vpu-heap-alias-call.py')
    spec = importlib.util.spec_from_file_location('request_v2_relocation', path)
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    if len(sys.argv) != 2:
        raise SystemExit('usage: patch-guest-info-request-v2.py mtgpu.ko')
    module = Path(sys.argv[1])
    data = module.read_bytes()
    helper = patch.load_elf_helpers()
    sections, named = helper.section_table(data)
    _, _, caller = patch.find_symbol(data, sections, 'mtgpu_device_memory_fixup')
    _, text = named['.text']
    start = text[4] + caller - text[3]
    guards = {
        0x24a: bytes.fromhex('48 8d bb ff 0f 00 00'),  # >=4 KiB buffer
        0x26f: bytes.fromhex('00 e8 00 00 00 00'),  # info pointer stored before call
        0x275: bytes.fromhex('49 8b 94 24 08 11 00 00 48 89 82 c8 00 00 00'),
    }
    for offset, expected in guards.items():
        if data[start+offset:start+offset+len(expected)] != expected:
            raise SystemExit(f'v2 request refused: info-buffer instruction guard {offset:#x}')
    patch.CALLER = 'mtgpu_device_memory_fixup'
    patch.ORIGINAL = 'os_virt_to_phys'
    patch.WRAPPER = 'mtgpu_guest_info_request_v2'
    patch.CALL_OFFSET = 0x270
    # Reuse the guarded relocation implementation; preserve its prior report.
    report = module.parent / 'guest-vpu-heap-alias-call-validation.json'
    previous = report.read_bytes() if report.exists() else None
    result = patch.main()
    report.rename(module.parent / 'guest-info-request-v2-validation.json')
    if previous is not None:
        report.write_bytes(previous)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
