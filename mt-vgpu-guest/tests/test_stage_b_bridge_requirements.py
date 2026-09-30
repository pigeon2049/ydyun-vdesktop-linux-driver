#!/usr/bin/env python3
"""Tests for the Stage B bridge requirement table.

The table is what a future kernel bridge will be built against, so its
structural claims are pinned here: the commands a real session exercises, the
line sizes the driver actually puts on the wire, the header-derived struct
layouts, and the mmap-offset invariant the kernel must honour.
"""

import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "build-stage-b-bridge-requirements.py"
DOC = ROOT / "reports" / "stage-b-bridge-requirements.json"

spec = importlib.util.spec_from_file_location("stage_b_requirements", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def key_of(bridge, function):
    return (int(bridge, 16), int(function, 16))


class BridgeCommandTable(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads(DOC.read_text())
        cls.entries = {key_of(e["bridge"], e["function"]): e
                       for e in cls.document["entries"]}

    def test_command_names_resolve(self):
        """Only the commands with no header in any tree stay unnamed.

        0x88:0x5-0x7 exist in the UMD but not in the 2.7.1 RGXKICKSYNC header
        (which stops at +4) nor in the 5.2 Host package (which omits the RGX
        groups entirely). They stay unnamed on purpose so the gap is visible.
        """
        unnamed = {(entry["bridge"], entry["function"]) for entry
                   in self.document["entries"] if not entry["command"]}
        self.assertEqual(unnamed, {("0x88", "0x5"), ("0x88", "0x6"), ("0x88", "0x7")})

    def test_phase_one_layouts_are_complete(self):
        """Every command in the verified session has fully parsed layouts."""
        for entry in self.document["required_now"]:
            entry = self.entries[key_of(entry["bridge"], entry["function"])]
            for direction in ("in_struct", "out_struct"):
                struct = entry.get(direction)
                if struct:
                    self.assertEqual(struct["unparsed_lines"], [],
                                     "%s of %s:%s" % (direction, entry["bridge"],
                                                      entry["function"]))

    def test_umd_wire_sizes_exceed_some_header_structs(self):
        """Pinned ABI deltas: the driver sends more bytes than 5.2 declares.

        The kernel bridge must accept the driver's sizes, not the header's, or
        the extra fields get dropped on the floor.
        """
        deltas = {}
        for entry in self.document["required_now"]:
            entry = self.entries[key_of(entry["bridge"], entry["function"])]
            for item in entry.get("size_disagreements", []):
                deltas.setdefault(entry["command"], []).append(
                    (item["direction"], item["umd_declared"], item["header_size"]))
        self.assertEqual(deltas.get("PHYSMEMNEWRAMBACKEDPMR"),
                         [("IN", 72, 68), ("OUT", 24, 20)])
        self.assertEqual(deltas.get("DEVMEMINTMAPPMR"), [("IN", 32, 28)])

    def test_observed_session_command_set(self):
        observed = {tuple(item.split(":")) for item in
                    (("%s:%s" % (r["bridge"], r["function"]))
                     for r in self.document["required_now"])}
        expected = {
            ("0x1", "0x0"), ("0x1", "0x2"), ("0x1", "0x4"), ("0x1", "0xa"),
            ("0x1", "0xc"), ("0x1", "0xf"), ("0x2", "0x0"), ("0x2", "0x7"),
            ("0x6", "0x3"), ("0x6", "0x6"), ("0x6", "0x9"), ("0x6", "0xf"),
            ("0x6", "0x11"), ("0x6", "0x13"), ("0x6", "0x15"), ("0x6", "0x1e"),
            ("0x6", "0x20"), ("0x82", "0x8"), ("0x86", "0x4"),
        }
        self.assertEqual(observed, expected)

    def test_sync_block_wire_sizes_and_layout(self):
        entry = self.entries[(0x2, 0x0)]
        self.assertEqual(entry["command"], "ALLOCSYNCPRIMITIVEBLOCK")
        self.assertEqual(entry["umd_input_size"], 8)
        self.assertEqual(entry["umd_output_size"], 32)
        out = entry["out_struct"]
        self.assertEqual(out["size"], 32)
        offsets = {field["name"]: field["offset"] for field in out["fields"]}
        self.assertEqual(offsets, {"hSyncHandle": 0, "hhSyncPMR": 8,
                                   "eError": 16, "ui32SyncPrimBlockSize": 20,
                                   "ui64SyncPrimVAddr": 24})
        self.assertEqual(out["unparsed_lines"], [])

    def test_connect_out_layout_matches_the_wire(self):
        """17 bytes both sides: u64 Bvnc, u32 eError, u32 caps, u8 arch."""
        entry = self.entries[(0x1, 0x0)]
        self.assertEqual(entry["umd_output_size"], 17)
        self.assertEqual(entry["out_struct"]["size"], 17)
        self.assertNotIn("size_disagreements", entry)
        offsets = {field["name"]: field["offset"]
                   for field in entry["out_struct"]["fields"]}
        self.assertEqual(offsets, {"ui64PackedBvnc": 0, "eError": 8,
                                   "ui32CapabilityFlags": 12, "ui8KernelArch": 16})

    def test_heap_details_layout(self):
        entry = self.entries[(0x6, 0x20)]
        self.assertEqual(entry["command"], "HEAPCFGHEAPDETAILS")
        self.assertEqual(entry["umd_input_size"], 20)
        self.assertEqual(entry["umd_output_size"], 44)
        fields = {field["name"]: field["offset"]
                  for field in entry["out_struct"]["fields"]}
        self.assertEqual(fields["sDevVAddrBase"], 0)
        self.assertEqual(fields["uiHeapLength"], 8)
        self.assertEqual(fields["uiReservedRegionLength"], 16)
        self.assertEqual(fields["puiHeapNameOut"], 24)
        self.assertEqual(fields["eError"], 32)
        self.assertEqual(fields["ui32Log2DataPageSizeOut"], 36)
        self.assertEqual(fields["ui32Log2ImportAlignmentOut"], 40)

    def test_struct_layouts_have_no_unparsed_lines(self):
        """Every fully parsed struct in the table parsed all of its fields."""
        parsed = 0
        for entry in self.document["entries"]:
            for direction in ("in_struct", "out_struct"):
                struct = entry.get(direction)
                if struct and not struct["unparsed_lines"]:
                    parsed += 1
        self.assertGreater(parsed, 200)


class TraceInvariants(unittest.TestCase):
    """Facts the shim traces pin down that the kernel side must reproduce."""

    TRACES = ["umd-bridge-connect-trace.jsonl", "umd-bridge-devmem-trace.jsonl",
              "umd-bridge-renderctx-trace.jsonl", "umd-bridge-sync-trace.jsonl"]

    def rows(self):
        import json as _json
        for name in self.TRACES:
            path = ROOT / "reports" / name
            if not path.is_file():
                continue
            for line in path.read_text(errors="replace").splitlines():
                try:
                    yield _json.loads(line)
                except ValueError:
                    continue

    def test_mmap_offset_is_handle_shifted_by_twelve(self):
        issued = set()
        for row in self.rows():
            written = row.get("out_written")
            if row.get("op") == "ioctl" and written:
                blob = bytes.fromhex(written)
                for index in range(0, len(blob) - 7, 8):
                    issued.add(int.from_bytes(blob[index:index + 8], "little"))
        seen = 0
        for row in self.rows():
            if row.get("op") != "mmap_fabricated":
                continue
            offset = int(row["off"], 16)
            self.assertEqual(offset & 0xfff, 0, "mmap offset not 4 KiB aligned")
            self.assertIn(offset >> 12, issued,
                          "mmap offset %s has no issued handle" % row["off"])
            seen += 1
        self.assertGreaterEqual(seen, 20, "trace set lost the mmap evidence")

    def test_sync_memtype_is_the_dynamic_value(self):
        for row in self.rows():
            if row.get("bridge") == "0x2:0x0" and row.get("op") == "ioctl":
                payload = bytes.fromhex(row["in"])
                self.assertEqual(len(payload), 8)
                self.assertEqual(int.from_bytes(payload, "little"),
                                 0x100000000)
                return
        self.fail("no AllocSyncPrimitiveBlock call in the committed traces")


if __name__ == "__main__":
    unittest.main()
