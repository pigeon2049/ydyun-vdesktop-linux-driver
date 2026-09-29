#!/usr/bin/env python3
"""Cross-check Guest map flags against two original driver instruction paths.

Windows MMUMapRange allocators/address translation are modeled; its flag
translation and PC/PD/PTE code execute. Linux RGXDerivePTEProt8 executes its
valid-input leaf path, skipping only __fentry__. No hardware access.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_RAX, UC_X86_REG_RSP

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'build/official-vgpu-r22b-mmu-mapping-audit-20260929/mtgpu.ko'
spec = importlib.util.spec_from_file_location('mmu_ref', ROOT / 'scripts/verify-mmu-bootstrap.py')
mmu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mmu)


def main():
    assert hashlib.sha256(MODULE.read_bytes()).hexdigest() == 'e2e47d2d581ab1988542e2639097226aed1f4b94efe596e5f11e89aabfcd60aa'
    symbols = subprocess.check_output(['nm', '-S', str(MODULE)], text=True)
    symbol = next(line.split() for line in symbols.splitlines() if line.endswith(' RGXDerivePTEProt8'))
    address, size = [int(x, 16) for x in symbol[:2]]
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'text.bin'
        subprocess.run(['objcopy', '--dump-section', f'.text={path}', str(MODULE)], check=True)
        code = path.read_bytes()[address:address + size]
    assert code[:5] == b'\xe8\0\0\0\0'  # __fentry__ relocation in ET_REL.
    machine = Uc(UC_ARCH_X86, UC_MODE_64)
    machine.mem_map(address & ~4095, 8192)
    machine.mem_write(address, code)
    machine.mem_map(0x200000, 8192)
    stop = 0x201000

    def guard(uc, pc, length, _):
        assert address + 5 <= pc < address + 0xad, hex(pc)  # no debug/error/import path

    machine.hook_add(UC_HOOK_CODE, guard)
    cases = []
    # Linux mmu_common.h defines VALID=1, READABLE=2, WRITEABLE=4,
    # CACHE_COHERENT=8 and DEVICE(n)=(n<<16).
    for flag in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):
        linux = 1 | 2 | (0 if flag & 1 else 4) | (8 if flag & 2 else 0) | (0x10000 if flag & 0x10 else 0)
        machine.mem_write(0x200800, struct.pack('<Q', stop))
        machine.reg_write(UC_X86_REG_RSP, 0x200800)
        machine.reg_write(UC_X86_REG_RDI, linux)
        machine.emu_start(address + 5, stop, timeout=100000, count=100)
        protection = machine.reg_read(UC_X86_REG_RAX)
        reference = mmu.MMUOracle(table_pa=0x61000d000)
        assert reference.map_range(mmu.Range(0x40200000, 0x61002f000, 4096, flag))
        pte = struct.unpack_from('<Q', reference.image(), 0x2000)[0]
        raw = pte & ~0xfffffff000
        assert raw == protection & ~0x3c00000000000000
        assert bool(pte & 2) == bool(flag & 1)
        cases.append({'guest_map_flags': flag, 'linux_protection_flags': linux,
                      'windows_pte': hex(pte), 'linux_pte_protection': hex(protection),
                      'read_only': bool(pte & 2), 'coherent': bool(pte & 4)})
    live = (ROOT / 'build/r23-live/tqx-after-root.bin').read_bytes()
    # Destination PT is the fourth allocated table page in the saved root.
    live_pte = struct.unpack_from('<Q', live, 0x3000)[0]
    assert live_pte == 0x61002f007
    report = {'passed': True, 'cases': cases, 'r23_destination_pte': hex(live_pte),
              'r23_destination_read_only': True, 'correct_default_pte': '0x61002f001',
              'linux_symbol': symbol[-1], 'linux_code_sha256': hashlib.sha256(code).hexdigest(),
              'windows_sha256': reference.pe.sha256, 'hardware_access': False,
              'limits': 'Proves the read-only mapping defect, not that it is the sole cause of the r23 firmware fault. Linux adds high PTE metadata absent from the Windows encoder; that remains a separate compatibility question.'}
    (ROOT / 'reports/r24-map-permissions.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
