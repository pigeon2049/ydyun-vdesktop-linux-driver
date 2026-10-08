#!/usr/bin/env python3
"""r374 gate: per-file TA VM interface layout (mt_ta_vm.h).

Pins the R5 design interface: VA reservation constants and struct
alignment. Reverse validation: altering an expected value must fail.
"""
import re
import sys
from pathlib import Path

HDR = Path(__file__).resolve().parents[1] / "kernel" / "mt_ta_vm.h"

def read():
    return HDR.read_text()

def test_va_base():
    src = read()
    m = re.search(r"#define MT_TA_CMD_VA_BASE\s+(0x[0-9a-fA-F]+)ULL", src)
    assert m, "MT_TA_CMD_VA_BASE not found"
    assert int(m.group(1), 16) == 0x70000000, f"unexpected base {m.group(1)}"

def test_va_size():
    src = read()
    m = re.search(r"#define MT_TA_CMD_VA_SIZE\s+(0x[0-9a-fA-F]+)ULL", src)
    assert m, "MT_TA_CMD_VA_SIZE not found"
    assert int(m.group(1), 16) == 0x1000, f"unexpected size {m.group(1)}"

def test_vm_context_has_vm_and_ready():
    src = read()
    assert "struct mt_gpu_vm *vm;" in src, "vm field missing"
    assert "bool ready;" in src, "ready flag missing"
    assert "owner_file" in src, "owner_file missing"

def test_cmd_mapping_has_gpu_va():
    src = read()
    assert "u64 gpu_va;" in src, "gpu_va missing"
    assert "borrowed_bo" in src, "borrowed_bo missing"
    assert "pinned_pages" in src, "pinned_pages missing"

def test_static_asserts_present():
    src = read()
    assert "_Static_assert" in src, "static asserts missing"
    assert "MT_TA_VM_READY" in src, "ready macro missing"

def test_no_implementation_logic():
    # Design header: declarations and asserts only, no function bodies.
    src = read()
    # Allow static inline? No — r374 is pure interface. Flag any '{' after
    # a function-like signature as implementation creep.
    bodies = re.findall(r"\)\s*\{\s*\n", src)
    assert not bodies, f"implementation body found in design header: {bodies}"

if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for t in tests:
        try:
            t()
            print(f"PASS {t.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {t.__name__}: {e}")
    sys.exit(1 if failed else 0)
