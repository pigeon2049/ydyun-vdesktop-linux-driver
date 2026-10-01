#!/usr/bin/env python3
"""Gate the kernel wire structs against the generated requirement table.

`kernel/mt_pvr_wire.h` mirrors the line formats the legacy MASA driver speaks.
The sizes it asserts come from the 5.2 KMD headers plus the wire sizes captured
in `reports/stage-b-bridge-requirements.json`. If a header refresh moves a
field, the C side and the table must both change -- this test compiles the
header and diffs the two, so neither can drift alone.
"""

import importlib.util
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "kernel" / "mt_pvr_wire.h"
TABLE = ROOT / "reports" / "stage-b-bridge-requirements.json"

spec = importlib.util.spec_from_file_location("stage_b_requirements",
                                              ROOT / "scripts" /
                                              "build-stage-b-bridge-requirements.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

# struct name -> (bridge, function) it must agree with. Only the commands whose
# layout the bridge models field by field appear here; the rest are exercised
# by their sizes only.
MAPPING = {
    "mt_pvr_cmd": None,
    "mt_pvr_connect_in": (0x1, 0x0),
    "mt_pvr_connect_out": (0x1, 0x0),
    "mt_pvr_handle_out": (0x1, 0xF),
    "mt_pvr_event_open_in": (0x1, 0x4),
    "mt_pvr_event_open_out": (0x1, 0x4),
    "mt_pvr_import_in": (0x6, 0x6),
    "mt_pvr_import_out": (0x6, 0x6),
    "mt_pvr_heap_details_in": (0x6, 0x20),
    "mt_pvr_heap_details_out": (0x6, 0x20),
    "mt_pvr_pmr_in": (0x6, 0x9),
    "mt_pvr_pmr_out": (0x6, 0x9),
    "mt_pvr_map_in": (0x6, 0x13),
    "mt_pvr_map_out": (0x6, 0x13),
    "mt_pvr_reserve_in": (0x6, 0x15),
    "mt_pvr_reserve_out": (0x6, 0x15),
    "mt_pvr_sync_block_in": (0x2, 0x0),
    "mt_pvr_sync_block_out": (0x2, 0x0),
    "mt_pvr_ctx_create_in": (0x6, 0xF),
    "mt_pvr_ctx_create_out": (0x6, 0xF),
    # 0x86:0x4 reuses mt_pvr_handle_out. 0x86:0x5 appeared in the S2 real-UMD
    # trace (in=8, out=4), past the 19-command scope, and needed for the UMD
    # to keep going; the 5.2 generated header calls them
    # MUSA:MUSAReleaseHWPerfSettings and declares the in as a single handle.
    "mt_pvr_hwperf_release_in": (0x86, 0x5),
    "mt_pvr_hwperf_release_out": (0x86, 0x5),
    # 0x6:0x1e carries {eError, ui32NumHeaps} -- eError FIRST. Writing the
    # count at offset 0 made the UMD cache zero heaps and fail every later
    # heap lookup (bA25).
    "mt_pvr_heap_count_out": (0x6, 0x1E),
    # 0x6:0x11 was misrouted to the PMR-map handler, which parsed a different
    # 28-byte struct and answered -EINVAL (bA25).
    "mt_pvr_heap_create_in": (0x6, 0x11),
    "mt_pvr_heap_create_out": (0x6, 0x11),
    # 0x6:0x27 MTGPUUpdateOOMStats fell through to -ENOTTY and the UMD
    # treated that as fatal (bA28).
    "mt_pvr_oom_stats_in": (0x6, 0x27),
    "mt_pvr_oom_stats_out": (0x6, 0x27),
    "mt_pvr_heap_destroy_in": (0x6, 0x12),
    "mt_pvr_heap_destroy_out": (0x6, 0x12),
}

# Which table size each struct corresponds to: "in", "out" or "dispatch".
DIRECTION = {
    "mt_pvr_cmd": "dispatch",
    "mt_pvr_connect_in": "in", "mt_pvr_connect_out": "out",
    "mt_pvr_handle_out": "out",
    "mt_pvr_event_open_in": "in", "mt_pvr_event_open_out": "out",
    "mt_pvr_import_in": "in", "mt_pvr_import_out": "out",
    "mt_pvr_heap_details_in": "in", "mt_pvr_heap_details_out": "out",
    "mt_pvr_pmr_in": "in", "mt_pvr_pmr_out": "out",
    "mt_pvr_map_in": "in", "mt_pvr_map_out": "out",
    "mt_pvr_reserve_in": "in", "mt_pvr_reserve_out": "out",
    "mt_pvr_sync_block_in": "in", "mt_pvr_sync_block_out": "out",
    "mt_pvr_ctx_create_in": "in", "mt_pvr_ctx_create_out": "out",
    "mt_pvr_hwperf_release_in": "in", "mt_pvr_hwperf_release_out": "out",
    "mt_pvr_heap_count_out": "out",
    "mt_pvr_heap_create_in": "in", "mt_pvr_heap_create_out": "out",
    "mt_pvr_heap_destroy_in": "in", "mt_pvr_heap_destroy_out": "out",
    "mt_pvr_oom_stats_in": "in", "mt_pvr_oom_stats_out": "out",
}

# Table key holding the wire size for each direction.
SIZE_KEY = {"in": "umd_input_size", "out": "umd_output_size"}


def declared_sizes():
    """Sizes the header asserts, read straight out of its static_asserts."""
    sizes = {}
    text = HEADER.read_text()
    for name, value in re.findall(
            r"static_assert\(sizeof\(struct (\w+)\) == (\d+)", text):
        sizes[name] = int(value)
    return sizes


def compiled_sizes():
    """Compile the header and print every struct size, so the two cannot drift."""
    with tempfile.TemporaryDirectory(prefix="mt-vgpu-wire-sizes-") as work:
        work = Path(work)
        source = work / "sizes.c"
        body = "\n".join([
            '#include "mt_pvr_wire.h"',
            '#include <stdio.h>',
            "int main(void) {",
        ])
        for name in MAPPING:
            body += '\tprintf("%s %%zu\\n", sizeof(struct %s));' % (name, name)
        body += "\treturn 0;}\n"
        source.write_text(body)
        binary = work / "sizes"
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "kernel"), str(source), "-o", str(binary)],
                       check=True, capture_output=True)
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    return {name: int(value) for name, value in
            (line.split() for line in out.splitlines() if line)}


class WireStructs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = json.loads(TABLE.read_text())
        cls.entries = {(e["bridge_id"], e["function_id"]): e
                       for e in cls.table["entries"]}
        cls.declared = declared_sizes()
        cls.compiled = compiled_sizes()

    def test_every_struct_is_covered_by_the_mapping(self):
        self.assertEqual(set(self.compiled), set(MAPPING))
        self.assertEqual(set(self.declared), set(MAPPING))

    def test_static_asserts_match_the_compiler(self):
        for name, size in self.compiled.items():
            self.assertEqual(self.declared[name], size,
                             "static_assert for %s says %d, compiler says %d"
                             % (name, self.declared[name], size))

    def test_sizes_match_the_wire_capture(self):
        for name, key in MAPPING.items():
            if key is None:
                continue
            entry = self.entries[key]
            if DIRECTION[name] == "dispatch":
                self.assertEqual(self.compiled[name], 32)
                continue
            expected = entry[SIZE_KEY[DIRECTION[name]]]
            self.assertEqual(self.compiled[name], expected,
                             "%s (%s) is %d, the driver sends %s"
                             % (name, entry["command"], self.compiled[name],
                                expected))

    def test_wire_deltas_are_modelled_as_reserved(self):
        """0x6:0x9 and 0x6:0x13 carry more bytes than the header declares."""
        for name, key, direction in (("mt_pvr_pmr_in", (0x6, 0x9), "in"),
                                     ("mt_pvr_pmr_out", (0x6, 0x9), "out"),
                                     ("mt_pvr_map_in", (0x6, 0x13), "in")):
            entry = self.entries[key]
            delta = {item["direction"]: item for item in
                     entry["size_disagreements"]}
            self.assertIn(direction.upper(), delta,
                          "%s no longer differs from the header" % name)
            self.assertEqual(self.compiled[name], delta[direction.upper()]["umd_declared"])


if __name__ == "__main__":
    unittest.main()
