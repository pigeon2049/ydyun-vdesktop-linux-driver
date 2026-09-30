#!/usr/bin/env python3
"""Pin the guest heap plan against the Windows reference table.

`mtkm64.sys` ships two static 22-entry GPU-VA heap tables; the S3000 takes the
MMU-mode-0 one. Our `mt_guest_plan_heaps()` fills the first 11 entries, and every
non-empty entry must match the reference byte for byte -- that equivalence is
what lets the Stage B bridge serve the vendor UMD a heap table we did not
invent. `scripts/dump-windows-heap-table.py` regenerates the JSON artifact.
"""

import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "dump-windows-heap-table.py"
ARTIFACT = ROOT / "reports" / "windows-heap-table-22.json"

spec = importlib.util.spec_from_file_location("windows_heap_table", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class WindowsHeapTable(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads(ARTIFACT.read_text())
        cls.entries = {row["index"]: row for row in cls.document["entries"]}

    def test_reference_hash_is_pinned(self):
        self.assertEqual(self.document["reference"]["sha256"],
                         "0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33")

    def test_table_has_twenty_two_slots(self):
        self.assertEqual(self.document["reference"]["heap_count"], 22)
        self.assertEqual(len(self.entries), 22)

    def test_mode_zero_descriptor_ids_are_sequential(self):
        """Field +0 of each descriptor is the heap's own id, 0x00..0x15."""
        for index, row in self.entries.items():
            self.assertEqual(row["verdict"] in ("identical", "both-empty"), True)
        pe = module.ReferencePE(module.REFERENCE)
        blob = pe.read(0x141030D90, 22 * 24)
        for index in range(22):
            identifier = int.from_bytes(blob[index * 24:index * 24 + 8], "little")
            self.assertEqual(identifier, index,
                             "heap %d carries id 0x%x" % (index, identifier))

    def test_every_guest_scope_heap_matches(self):
        """Slots 4 and 5 are empty on both sides; the rest must match exactly."""
        for index in range(module.GUEST_HEAP_COUNT):
            row = self.entries[index]
            self.assertIn(row["verdict"], ("identical", "both-empty"),
                          "heap %d: windows 0x%x+0x%x vs guest 0x%x+0x%x"
                          % (index, row["windows_base"], row["windows_size"],
                             row["guest_base"], row["guest_size"]))
            self.assertEqual(row["windows_base"], row["guest_base"])
            self.assertEqual(row["windows_size"], row["guest_size"])

    def test_non_empty_heaps_are_exactly_ours(self):
        non_empty = sorted(index for index, row in self.entries.items()
                           if row["windows_size"])
        self.assertEqual(non_empty, [0, 1, 2, 3, 6, 7, 8, 9, 10])

    def test_mode_one_tail_is_not_a_heap_table(self):
        """Mode 1 leaves slots 17+ holding unrelated data; do not trust them."""
        self.assertFalse(self.document["modes_identical"])
        pe = module.ReferencePE(module.REFERENCE)
        blob = pe.read(0x141030FA0, 22 * 24)
        tail = [int.from_bytes(blob[i * 24 + 8:i * 24 + 16], "little")
                for i in (19, 20, 21)]
        self.assertTrue(any(value > 0x1000000000 for value in tail),
                        "expected non-heap payloads in the mode-1 tail")

    def test_guest_plan_compiles_and_agrees(self):
        plan = module.guest_plan()
        for index in range(module.GUEST_HEAP_COUNT):
            row = self.entries[index]
            self.assertEqual(plan[index]["base"], row["windows_base"])
            self.assertEqual(plan[index]["size"], row["windows_size"])


if __name__ == "__main__":
    unittest.main()
