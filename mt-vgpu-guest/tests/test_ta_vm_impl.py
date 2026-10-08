#!/usr/bin/env python3
"""r375 gate: R5 TA VM implementation (mt_pvr_bridge.c).

Pins the R5 infrastructure: per-file VM context create/destroy, command
buffer map/unmap, V1/V2 hooks in musakickgfx2 dispatch, file-close cleanup.
Reverse validation: removing an expected symbol must fail.
"""
import re
import sys
from pathlib import Path

BRIDGE = Path(__file__).resolve().parents[1] / "kernel" / "recovery" / "mt_pvr_bridge.c"

def read():
    return BRIDGE.read_text()

def test_ta_vm_header_included():
    src = read()
    assert '#include "../mt_ta_vm.h"' in src, "mt_ta_vm.h not included"

def test_file_has_ta_vm_ctx():
    src = read()
    assert "struct mt_ta_vm_context *ta_vm_ctx;" in src, "ta_vm_ctx field missing"

def test_context_create_defined():
    src = read()
    assert "static struct mt_ta_vm_context *mt_ta_vm_context_create" in src, \
        "create not defined"

def test_context_destroy_defined():
    src = read()
    assert "static void mt_ta_vm_context_destroy" in src, "destroy not defined"

def test_map_defined():
    src = read()
    assert "static int mt_ta_vm_map_cmd_buffer" in src, "map not defined"

def test_unmap_defined():
    src = read()
    assert "static void mt_ta_vm_unmap_cmd_buffer" in src, "unmap not defined"

def test_v1_hook():
    src = read()
    assert "R5 V1: TA VM context created" in src, "V1 hook missing"

def test_v2_hook():
    src = read()
    assert "R5 V2:" in src, "V2 hook missing"

def test_cleanup_hook():
    src = read()
    assert "mt_ta_vm_context_destroy(file->ta_vm_ctx)" in src, "cleanup missing"

def test_uses_real_borrow():
    # Red line: must use real mt_bo_system_borrow, not a fake.
    src = read()
    assert "mt_bo_system_borrow" in src, "borrow not used"
    assert "mt_gpu_vm_bind_many" in src, "bind_many not used"
    assert "pin_user_pages" in src, "pin_user_pages not used"

def test_gate_closed():
    # r375: gate stays closed; marker path unaffected. The map is
    # validation-only (result logged, then unmapped).
    src = read()
    assert "Gate MT_TA_VM_READY stays" in src or "gate closed" in src.lower() or \
        "Gate closed" in src, "gate-closed comment missing"

if __name__ == "__main__":
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for fn in fns:
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}")
            failed += 1
    sys.exit(1 if failed else 0)
