#!/usr/bin/env python3
"""Redirect the guarded Devmem context call through create/destroy/stop audit."""
import importlib.util
from pathlib import Path
import sys

def main():
    spec = importlib.util.spec_from_file_location('mmu_audit_relocation',
        Path(__file__).with_name('patch-guest-vpu-heap-alias-call.py'))
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    if len(sys.argv) != 2:
        raise SystemExit('usage: patch-guest-mmu-context-audit.py mtgpu.ko')
    module = Path(sys.argv[1])
    if b'MT_MMU_CONTEXT destroyed=1 publication_blocked=1' not in module.read_bytes():
        raise SystemExit('context stop wrapper is missing')
    patch.CALLER = 'DevmemIntCtxCreate'
    patch.ORIGINAL = 'MMU_ContextCreate'
    patch.WRAPPER = 'mtgpu_guest_mmu_context_audit'
    patch.CALL_OFFSET = 0x79
    report = module.parent / 'guest-vpu-heap-alias-call-validation.json'
    previous = report.read_bytes() if report.exists() else None
    result = patch.main()
    report.rename(module.parent / 'guest-mmu-context-audit-validation.json')
    if previous is not None:
        report.write_bytes(previous)
    patch.CALLER = 'RGXInit'
    patch.ORIGINAL = 'RGXInitCreateFWKernelMemoryContext'
    patch.WRAPPER = 'mtgpu_guest_fw_context_audit'
    patch.CALL_OFFSET = 0x588
    result = patch.main()
    report.rename(module.parent / 'guest-fw-context-audit-validation.json')
    if previous is not None:
        report.write_bytes(previous)
    return result

if __name__ == '__main__':
    raise SystemExit(main())
