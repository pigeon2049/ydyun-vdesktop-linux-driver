#!/usr/bin/env python3
"""r394 T1 gate: no manual struct mt_gpu_vm assembly outside mt_gpu_vm.h.

r375 postmortem: the bridge hand-assembled a struct mt_gpu_vm, omitting
the 'ranges'/'page_lists' fields that mt_gpu_vm_bind_many() dereferences.
Result: kernel oops (RIP mt_gpu_vm_bind_many+0x300), D-state harness,
bridge refcount stuck -> user cold reboot.

Rule enforced here: every VM creation must go through mt_gpu_vm_init()
(or a wrapper such as mt_bridge_ta_vm_create / mt_render_context_vm_create
that calls it). Direct assignment to the VM's internal fields anywhere
outside kernel/mt_gpu_vm.h (the VM's own implementation) fails the gate.

Reverse validation: add e.g. `foo->ranges = 0;` to any kernel/*.c file ->
test_no_manual_vm_field_assignment must FAIL.
"""
import re
import unittest
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[1] / "kernel"
VM_IMPL = KERNEL / "mt_gpu_vm.h"

# Internal fields of struct mt_gpu_vm. Only mt_gpu_vm.h (init/grow/fini)
# may assign them. r375 missed 'ranges' and 'page_lists' -> oops.
FORBIDDEN_FIELDS = ("ranges", "page_lists", "bindings",
                    "binding_capacity", "max_ranges")
FIELD_ASSIGN_RE = re.compile(
    r"(?:->|\.)(" + "|".join(FORBIDDEN_FIELDS) + r")\s*=(?![=>])"
)
# `struct mt_gpu_vm x = { ... }` with non-zero content is manual assembly.
# A pure {0} followed by mt_gpu_vm_init() (selftest pattern) is allowed.
VM_DESIGNATED_INIT_RE = re.compile(
    r"struct\s+mt_gpu_vm\s+\w[\w\s\*]*=\s*\{(?![\s}]*0[\s}])"
)


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"(?m)//.*$", "", src)
    return src


def _iter_sources():
    for pat in ("*.c", "*.h"):
        for p in sorted(KERNEL.rglob(pat)):
            if p == VM_IMPL:
                continue
            yield p


def _violations():
    out = []
    for p in _iter_sources():
        src = _strip_comments(p.read_text())
        for m in FIELD_ASSIGN_RE.finditer(src):
            lineno = src.count("\n", 0, m.start()) + 1
            out.append("%s:%d: forbidden struct mt_gpu_vm.%s assignment"
                       % (p.name, lineno, m.group(1)))
        for m in VM_DESIGNATED_INIT_RE.finditer(src):
            lineno = src.count("\n", 0, m.start()) + 1
            out.append("%s:%d: manual struct mt_gpu_vm initializer"
                       % (p.name, lineno))
    return out


class TestVmInitIntegrity(unittest.TestCase):
    def test_no_manual_vm_field_assignment(self):
        v = _violations()
        self.assertEqual(
            v, [],
            "manual struct mt_gpu_vm assembly detected "
            "(r375 oops class) -- use mt_gpu_vm_init():\n" + "\n".join(v))

    def test_vm_impl_still_owns_fields(self):
        # Sanity: the VM's own implementation really does assign these
        # (init/grow paths). If this fails, FORBIDDEN_FIELDS is stale.
        src = _strip_comments(VM_IMPL.read_text())
        self.assertTrue(
            FIELD_ASSIGN_RE.search(src),
            "mt_gpu_vm.h no longer assigns VM internals; "
            "re-check FORBIDDEN_FIELDS")


if __name__ == "__main__":
    unittest.main()
