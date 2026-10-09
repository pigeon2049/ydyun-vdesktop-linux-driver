#!/usr/bin/env python3
"""Decode the Windows driver's own 22-entry heap table and pin our plan to it.

Read directly out of mtkm64.sys (sha256-pinned) at the two addresses the
decompilation names, so the geometry is a measurement rather than a note.

Layout, established by scanning for the field arrangement that reproduces our
existing plan byte for byte (0x18 stride, base at +0x8, size at +0x10):

    MMU+0x108 == 1 selects the table at 0x141030fa0, otherwise 0x141030d90.

The Windows table is the guest physical heap plan, not the PVR device heap
configuration served over HeapCfgHeapDetails. Confusing the two put the PDS
and USC names on small physical slots. The bridge now serves the vendor RGX
application blueprint instead; the Windows table remains the authority only
for guest-side physical resources.
"""
import hashlib
import json
import re
import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

GUEST = get_repo_root()
QUEUE_H = GUEST / 'kernel/mt_pvr_queue.h'
DRIVER = Path('/opt/MTT-driver-only/mtkm64.sys')

MTKM64_SHA256 = '0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33'
IMAGE_BASE = 0x140000000
TABLE_MODE_0 = 0x1030d90
TABLE_MODE_1 = 0x1030fa0
ENTRY_STRIDE = 0x18
# The table starts 8 bytes past the address the decompilation names: the copy
# loop advances by 0x18 qwords-worth of bytes per entry and the first base
# lands at +0x8, not +0. Found by scanning for the arrangement that reproduces
# the existing plan; getting it wrong silently shifts every field.
ENTRY_OFFSET = 8
HEAP_SLOTS = 11

# The guest physical plan served to guest-side VM resources (not the PVR heap
# configuration served to the UMD).
PLAN_BASES = [
    0x40000000, 0x8100000000, 0x8400000000, 0xa000000000, 0, 0,
    0xe1c0000000, 0xec00000000, 0xec40000000, 0xeb00000000, 0xf000000000,
]
PLAN_SIZES = [
    0x8000000000, 0x100000000, 0x100000000, 0x1000000, 0, 0,
    0x100000000, 0x8000, 0x1000, 0x100000000, 0x100000000,
]
# Measured against the live 5.2 UMD under gdb.
UMD_PDS_SUBALLOC = 0x9000
UMD_PDS_HEAP_NAME = 'MemHeap:PDS_CODE'


def sections(blob):
    pe = struct.unpack_from('<I', blob, 0x3c)[0]
    nsec = struct.unpack_from('<H', blob, pe + 6)[0]
    optsz = struct.unpack_from('<H', blob, pe + 20)[0]
    so = pe + 24 + optsz
    out = []
    for i in range(nsec):
        b = so + i * 40
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', blob, b + 8)
        out.append((vaddr, max(vsize, rsize), raddr))
    return out


def read_table(blob, rva):
    secs = sections(blob)
    off = None
    for vaddr, span, raddr in secs:
        if vaddr <= rva < vaddr + span:
            off = raddr + (rva - vaddr)
            break
    if off is None:
        raise AssertionError(f'RVA {rva:#x} is in no section')
    out = []
    for i in range(HEAP_SLOTS):
        base = struct.unpack_from('<Q', blob, off + ENTRY_OFFSET + i * ENTRY_STRIDE)[0]
        size = struct.unpack_from('<Q', blob, off + ENTRY_OFFSET + i * ENTRY_STRIDE + 8)[0]
        out.append((base, size))
    return out


@unittest.skipUnless(DRIVER.exists(), 'mtkm64.sys is not present on this machine')
class WindowsHeapTableDecoded(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = DRIVER.read_bytes()
        cls.mode0 = read_table(cls.blob, TABLE_MODE_0)
        cls.mode1 = read_table(cls.blob, TABLE_MODE_1)

    def test_driver_is_the_one_we_cited(self):
        digest = hashlib.sha256(self.blob).hexdigest()
        self.assertEqual(digest, MTKM64_SHA256,
                         'mtkm64.sys differs from the build the heap table was '
                         'originally decoded from; every number here would be '
                         'about a different driver')

    def test_decoded_layout_reproduces_our_plan(self):
        # If this fails the field order or stride is wrong, and every other
        # assertion below is meaningless.
        self.assertEqual([b for b, _ in self.mode0], PLAN_BASES)
        self.assertEqual([s for _, s in self.mode0], PLAN_SIZES)

    def test_recorded_report_agrees_with_the_driver(self):
        report = GUEST / 'reports/windows-heap-table-22.json'
        data = json.loads(report.read_text())
        for row in data['entries']:
            if row['index'] >= HEAP_SLOTS:
                continue
            self.assertEqual(row['windows_size'], self.mode0[row['index']][1],
                             f"entry {row['index']} size disagrees with the "
                             'driver binary')

    def test_mmu_mode_1_drops_slot_3(self):
        # Recorded, because it is a real difference between the two tables and
        # it is the one place where publishing mode 0's geometry would be
        # wrong for a mode-1 device.
        #
        #   mode 0: slot 3 = base 0xa000000000, size 0x1000000 (Component
        #           Control by our binding)
        #   mode 1: slot 3 = empty
        #
        # The guest serves mode 0's geometry. Which mode the S3000 actually
        # reports has not been checked against a live device, so this is noted
        # rather than asserted either way. Do not "fix" mode 1 by copying
        # slot 3 across without first confirming which mode is in use.
        self.assertEqual(self.mode0[3][0], 0xa000000000)
        self.assertEqual(self.mode0[3][1], 0x1000000)
        self.assertEqual(self.mode1[3][1], 0,
                         'slot 3 is no longer empty in mode 1; the '
                         'difference between the two tables has changed and '
                         'the note above is stale')

    def test_both_modes_agree_on_the_slots_the_umd_needs(self):
        # Slots 6 through 10 -- the ones the UMD walks -- must be identical in
        # both modes, or the published table would depend on the flag.
        for i in range(6, HEAP_SLOTS):
            self.assertEqual(
                self.mode0[i], self.mode1[i],
                f'slot {i} differs between MMU modes: '
                f'{self.mode0[i]} vs {self.mode1[i]}')

    def test_empty_slots_are_empty_in_the_driver_too(self):
        # Justifies the compaction: these are genuinely unpopulated upstream,
        # not something we failed to fill in.
        for i, (_, size) in enumerate(self.mode0):
            if size == 0:
                self.assertEqual(self.mode0[i][0], 0)

    def test_windows_physical_slot_7_is_small(self):
        # This is a fact about the guest physical plan, not the PVR heap
        # served to the UMD. It must not be read as the size of PDS Code and
        # Data; confusing the two tables caused the render-context failure.
        self.assertEqual(self.mode0[7][1], 0x8000)
        self.assertLess(self.mode0[7][1], UMD_PDS_SUBALLOC)

    def test_slots_that_could_satisfy_the_umd_request(self):
        # Physical slots large enough for the measured request. This remains a
        # resource-planning fact; it is not the PVR heap lookup.
        big = [i for i, (_, size) in enumerate(self.mode0)
               if size >= UMD_PDS_SUBALLOC]
        self.assertIn(0, big)
        self.assertNotIn(7, big)

    def test_bridge_does_not_reuse_windows_physical_slots_for_pds(self):
        # The bridge must serve the vendor RGX blueprint, where PDS and USC
        # are 4 GiB heaps at 0xda and 0xe0 rather than the small Windows
        # physical slots.
        text = QUEUE_H.read_text()
        m = re.search(
            r'static const struct mt_pvr_heap_entry app_heaps\[\]\s*=\s*\{(.*?)\};',
            text, re.S)
        self.assertIsNotNone(m, 'the bridge blueprint is gone')
        rows = re.findall(
            r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)ULL\s*,\s*([0-9]+)ULL',
            m.group(1))
        by_name = {name: (int(base, 16), int(size)) for name, base, size in rows}
        self.assertEqual(by_name['PDS Code and Data'], (0xda00000000, 0x100000000))
        self.assertEqual(by_name['USC Code'], (0xe000000000, 0x100000000))

    def test_the_naming_mapping_is_documented_as_unproven(self):
        # Windows names resources, not heaps. The caveat must stay in the
        # crosscheck so the physical table is never again mistaken for the
        # PVR heap configuration.
        report = (GUEST / 'reports/windows-kmd-crosscheck.md').read_text()
        self.assertIn('从不给 22 个', report,
                      'the naming gap is no longer recorded in the crosscheck')


if __name__ == '__main__':
    unittest.main()