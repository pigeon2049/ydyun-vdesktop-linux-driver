#!/usr/bin/env python3
"""Exercise the Windows RM GPU-device heap-count selector in a bounded emulator.

Only FUN_14003c4d0 is executed from the vendor image. Its two initialization
helpers are replaced with success-return stubs, so this checks the selector
branch and output field, not device initialization or Windows behavior.
"""
import json
import hashlib
from pathlib import Path
import re
import sys

from reference_oracle import ReferenceOracle


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path("/opt/MTT-driver-only/mtkm64.sys")
BINARY_SHA256 = "0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33"
DISPLAY_BINARY = Path("/opt/MTT-driver-only/mtdispkm64.sys")
DISPLAY_BINARY_SHA256 = "e061a9bda23fe8ae0cee81522a8b89455bfddd0b295bc86704009fcdeb82a686"
CONSTRUCTOR = 0x14003c4d0
CONSTRUCTOR_END = 0x14003c64c
INIT_INTERRUPT = 0x14003e060
INIT_FW = 0x14003e1d8
OBJECT = 0x201000
ARGS = 0x202000
DESCRIPTOR = 0x203000


def exercise(selector):
    oracle = ReferenceOracle()
    x = oracle
    x.ranges.append((CONSTRUCTOR, CONSTRUCTOR_END))
    fw_selectors = []

    def success():
        x.ret(0)

    def fw_success():
        fw_selectors.append(x.arg(0))
        x.ret(0)

    x.hooks[INIT_INTERRUPT] = success
    x.hooks[INIT_FW] = fw_success

    x.uc.mem_write(OBJECT, bytes(0x200))
    x.uc.mem_write(ARGS, bytes(0x80))
    x.uc.mem_write(DESCRIPTOR, bytes(0x100))
    x.put64(ARGS + 0x18, DESCRIPTOR)
    x.put32(DESCRIPTOR, selector)
    # The constructor consults this optional firmware-region table after
    # selecting the heap count. Keep it null so only the selector is tested.
    x.put64(OBJECT + 0x28, 0)
    result = x.run(CONSTRUCTOR, [OBJECT, ARGS], x.ranges)
    count = int.from_bytes(x.uc.mem_read(OBJECT + 0x24, 4), "little")
    return {"selector": f"0x{selector:03x}", "return": result,
            "heap_count": count, "firmware_selector": fw_selectors}


def main():
    actual_hash = hashlib.sha256(BINARY.read_bytes()).hexdigest()
    assert actual_hash == BINARY_SHA256, (
        f"unexpected Windows driver image hash: {actual_hash}"
    )
    display_hash = hashlib.sha256(DISPLAY_BINARY.read_bytes()).hexdigest()
    assert display_hash == DISPLAY_BINARY_SHA256, (
        f"unexpected Windows display-driver image hash: {display_hash}"
    )
    display_c = (ROOT / "decompiled/mtdispkm64.sys/decompiled.c").read_text()
    assert re.search(r"uVar3\s*=\s*\*\(ushort \*\)\(param_2 \+ 2\);", display_c)
    assert re.search(r"uVar4\s*=\s*uVar3\s*&\s*0xff00;", display_c)
    assert re.search(r"uVar4\s*==\s*0x200\)\s*\{\s*FUN_140018100", display_c)
    cases = [exercise(value) for value in (0x100, 0x200, 0x300, 0x400)]
    expected = {0x100: 4, 0x200: 4, 0x300: 4, 0x400: 5}
    for row in cases:
        selector = int(row["selector"], 16)
        assert row["return"] == 0, row
        assert row["heap_count"] == expected[selector], row

    report = {
        "binary": str(BINARY),
        "binary_sha256": actual_hash,
        "related_pci_family_evidence": {
            "binary": str(DISPLAY_BINARY),
            "binary_sha256": display_hash,
            "function_va": "0x140017650",
            "device_id_field": "16-bit value at input +2",
            "family_mask": "0xff00",
            "quyuan1_selector": "0x0200",
            "profile_function_va": "0x140018100",
        },
        "registered_object": "gpu_device",
        "constructor_va": f"0x{CONSTRUCTOR:x}",
        "executed_range": [f"0x{CONSTRUCTOR:x}", f"0x{CONSTRUCTOR_END:x}"],
        "stubbed_helpers": [f"0x{INIT_INTERRUPT:x}", f"0x{INIT_FW:x}"],
        "cases": cases,
        "interpretation": (
            "The constructor's first descriptor dword 0x200 selects four heaps. "
            "The Linux source identifies S3000 0x0222 as QUYUAN1 family 0x0200; "
            "this is supporting cross-driver evidence for a four-heap profile, "
            "but the Windows descriptor field has not been proven identical to "
            "the Linux family mask. This does not validate PVZ map/unmap."
        ),
        "limits": [
            "Only the selector function ran; Windows kernel helpers were not executed.",
            "The result is static Windows RM behavior, not a Guest hardware trial.",
            "Windows Pci/BAR mapping is not treated as an implementation of Linux PVZ callbacks."
        ],
    }
    out = ROOT / "reports/windows-gpu-device-heap-count.json"
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    sys.exit(main())
