#!/usr/bin/env python3
"""Dump the Windows driver's 22-entry GPU-VA heap table and diff it against ours.

`mtkm64.sys` builds its memory plan in `FUN_14001ccd8` (decompiled.c:22804). It
copies one of two static descriptor tables, selected by the MMU mode at
`MMU+0x108` (disassembly.txt:31103): mode 1 uses `0x141030fa0`, every other mode
(S3000 reports mode 0) uses `0x141030d90`. Each descriptor is 24 bytes:

    +0x00  u64  (unused by the plan; the available copy is at +0x4f8)
    +0x08  u64  GPU virtual base
    +0x10  u64  size

The plan stores 22 entries (`decompiled.c:23169` -> `param_4 + 0xe1 = 0x16`).
Our own guest plan (`kernel/mt_guest_heaps.h:mt_guest_plan_heaps()`) fills the
first 11 of those slots, so this script settles which entries agree, which are
verifiably different, and which our side simply leaves empty.

Read-only: it parses the reference PE and compiles our in-tree header.
"""

import ctypes
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from pe_image import ReferencePE  # noqa: E402

REFERENCE = Path("/opt/MTT-driver-only/mtkm64.sys")
TABLES = {"mmu_mode_0": 0x141030D90, "mmu_mode_1": 0x141030FA0}
HEAP_COUNT = 22
DESCRIPTOR_SIZE = 24
GUEST_HEAP_COUNT = 11

OUTPUT = ROOT / "reports" / "windows-heap-table-22.json"


class Plan(ctypes.Structure):
    """Mirror of `struct mt_guest_heap_plan` from kernel/mt_guest_heaps.h."""

    class Heap(ctypes.Structure):
        _fields_ = [("base", ctypes.c_uint64), ("size", ctypes.c_uint64),
                    ("reserved_base", ctypes.c_uint64),
                    ("reserved_size", ctypes.c_uint64)]

    class Resource(ctypes.Structure):
        _fields_ = [("va", ctypes.c_uint64), ("size", ctypes.c_uint32),
                    ("heap", ctypes.c_uint32)]

    _fields_ = [("heaps", Heap * 22), ("resources", Resource * 13)]


def guest_plan():
    """Compile the in-tree header and call mt_guest_plan_heaps()."""
    with tempfile.TemporaryDirectory(prefix="mt-vgpu-heap-diff-") as work:
        work = Path(work)
        source = work / "probe.c"
        source.write_text(
            '#include "mt_guest_heaps.h"\n'
            "#include <stdio.h>\n"
            "int main(void){struct mt_guest_heap_plan p;"
            "mt_guest_plan_heaps(&p);"
            'fwrite(&p,sizeof(p),1,stdout);return 0;}\n')
        binary = work / "probe"
        subprocess.run(["cc", "-O0", "-I", str(ROOT / "kernel"), str(source),
                        "-o", str(binary)], check=True)
        raw = subprocess.run([str(binary)], check=True,
                             stdout=subprocess.PIPE).stdout
    plan = Plan.from_buffer_copy(raw[:ctypes.sizeof(Plan)])
    return [{"base": plan.heaps[i].base, "size": plan.heaps[i].size,
             "reserved_base": plan.heaps[i].reserved_base,
             "reserved_size": plan.heaps[i].reserved_size}
            for i in range(22)]


def read_table(pe, address):
    blob = pe.read(address, HEAP_COUNT * DESCRIPTOR_SIZE)
    return [{"index": i,
             "flag": struct.unpack_from("<Q", blob, i * 24)[0],
             "base": struct.unpack_from("<Q", blob, i * 24 + 8)[0],
             "size": struct.unpack_from("<Q", blob, i * 24 + 16)[0]}
            for i in range(HEAP_COUNT)]


def main():
    pe = ReferencePE(REFERENCE)
    tables = {name: read_table(pe, address) for name, address in TABLES.items()}
    ours = guest_plan()

    identical_tables = all(
        tables["mmu_mode_0"][i] == tables["mmu_mode_1"][i]
        for i in range(HEAP_COUNT))

    comparison = []
    for i in range(HEAP_COUNT):
        reference = tables["mmu_mode_0"][i]
        mine = ours[i]
        mine_in_scope = i < GUEST_HEAP_COUNT
        if reference["size"] == 0 and mine["size"] == 0:
            verdict = "both-empty"
        elif reference["base"] == mine["base"] and reference["size"] == mine["size"]:
            verdict = "identical"
        elif mine["size"] == 0 and reference["size"] == 0:
            verdict = "both-empty"
        elif mine["size"] == 0:
            verdict = "windows-only"
        elif reference["size"] == 0:
            verdict = "guest-only"
        else:
            verdict = "differs"
        comparison.append({
            "index": i,
            "windows_base": reference["base"],
            "windows_size": reference["size"],
            "guest_base": mine["base"],
            "guest_size": mine["size"],
            "guest_reserved_base": mine["reserved_base"],
            "guest_reserved_size": mine["reserved_size"],
            "guest_scope": mine_in_scope,
            "verdict": verdict,
        })

    summary = {}
    for row in comparison:
        summary[row["verdict"]] = summary.get(row["verdict"], 0) + 1

    document = {
        "purpose": "Windows 22-entry GPU-VA heap table vs our guest plan",
        "reference": {
            "path": str(REFERENCE),
            "sha256": pe.sha256,
            "tables": {name: hex(address) for name, address in TABLES.items()},
            "table_selection": "MMU+0x108 == 1 selects mmu_mode_1, else mmu_mode_0",
            "heap_count": HEAP_COUNT,
            "evidence": [
                "decompiled/mtkm64.sys/decompiled.c:23169 (param_4+0xe1 = 0x16)",
                "decompiled/mtkm64.sys/disassembly.txt:31103 (table selection)",
                "decompiled/mtkm64.sys/disassembly.txt:31107 (22 slots preset)",
            ],
        },
        "guest_plan": "kernel/mt_guest_heaps.h:mt_guest_plan_heaps()",
        "modes_identical": identical_tables,
        "summary": summary,
        "entries": comparison,
    }
    OUTPUT.write_text(json.dumps(document, indent=1) + "\n")

    print("wrote %s" % OUTPUT.relative_to(ROOT))
    print("modes identical: %s" % identical_tables)
    for verdict, count in sorted(summary.items()):
        print("  %-12s %d" % (verdict, count))
    for row in comparison:
        if row["verdict"] in ("differs", "windows-only", "guest-only"):
            print("  idx %2d %-12s windows 0x%x+0x%x  guest 0x%x+0x%x"
                  % (row["index"], row["verdict"], row["windows_base"],
                     row["windows_size"], row["guest_base"], row["guest_size"]))
    mismatched = [row for row in comparison[:GUEST_HEAP_COUNT]
                  if row["verdict"] not in ("identical", "both-empty")]
    if mismatched:
        print("FAIL: %d guest-scope heaps disagree with the reference"
              % len(mismatched))
        return 1
    print("OK: all %d guest-scope heaps match the reference" % GUEST_HEAP_COUNT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
