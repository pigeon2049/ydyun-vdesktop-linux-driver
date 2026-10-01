#!/usr/bin/env python3
"""Decode the Windows driver's own 22-entry heap table and pin our plan to it.

Read directly out of mtkm64.sys (sha256-pinned) at the two addresses the
decompilation names, so the geometry is a measurement rather than a note.

Layout, established by scanning for the field arrangement that reproduces our
existing plan byte for byte (0x18 stride, base at +0x8, size at +0x10):

    MMU+0x108 == 1 selects the table at 0x141030fa0, otherwise 0x141030d90.

The point of decoding it here is that it settles a naming question. The Windows
driver names its 13 resources but never its 22 heaps, so which slot is
"PDS Code and Data" has always been our inference. The UMD suballocates 0x9000
bytes from MemHeap:PDS_CODE, and slot 7 -- the one we call PDS Code and Data --
is only 0x8000, so that inference is wrong. The sizes themselves are correct
and must not be "fixed" to make the symptom go away.
"""
import hashlib
import json
import struct
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
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

# What we currently publish, and the name we bind to each slot.
PLAN_BASES = [
    0x40000000, 0x8100000000, 0x8400000000, 0xa000000000, 0, 0,
    0xe1c0000000, 0xec00000000, 0xec40000000, 0xeb00000000, 0xf000000000,
]
PLAN_SIZES = [
    0x8000000000, 0x100000000, 0x100000000, 0x1000000, 0, 0,
    0x100000000, 0x8000, 0x1000, 0x100000000, 0x100000000,
]
NAME_BY_SLOT = {
    0: 'General',
    3: 'Component Control',
    7: 'PDS Code and Data',
    8: 'USC Code',
}

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

    def test_pds_slot_is_too_small_for_what_the_umd_asks(self):
        # The reason the render context fails. Recorded as a fact about the
        # driver's geometry, so that raising slot 7's size is visibly a
        # contradiction of this table rather than a plausible fix.
        pds = self.mode0[7][1]
        self.assertEqual(pds, 0x8000)
        self.assertLess(pds, UMD_PDS_SUBALLOC,
                        'slot 7 is now big enough for the UMD request; if this '
                        'fires, re-check whether the naming mapping below has '
                        'been fixed properly instead of by enlarging the heap')

    def test_slots_that_could_satisfy_the_umd_request(self):
        # Which slots are even large enough, for whoever re-derives the
        # name-to-slot mapping.
        big = [i for i, (_, size) in enumerate(self.mode0)
               if size >= UMD_PDS_SUBALLOC]
        self.assertIn(0, big, 'the General heap must remain able to serve it')
        self.assertNotIn(7, big,
                         'slot 7 is expected to be too small; if this fires the '
                         'binding has been fixed properly rather than by '
                         'enlarging the slot')

    def test_both_small_slots_are_bound_to_names_we_cannot_support(self):
        # Slots 7 (32 KiB) and 8 (4 KiB) are the two the UMD could never
        # allocate from, yet both carry names. That is the whole defect, so it
        # is asserted rather than described in a comment.
        self.assertEqual(self.mode0[7][1], 0x8000)
        self.assertEqual(self.mode0[8][1], 0x1000)
        for slot in (7, 8):
            self.assertIn(slot, NAME_BY_SLOT)
            self.assertLess(self.mode0[slot][1], UMD_PDS_SUBALLOC,
                            f'slot {slot} carries a name but is far too small '
                            'for the allocation the UMD attempts against it')

    def test_the_naming_mapping_is_documented_as_unproven(self):
        # Windows names resources, not heaps, so every name in NAME_BY_SLOT is
        # an inference, and the PDS one is now known to be wrong. The caveat
        # must stay in the crosscheck until the mapping is re-derived.
        report = (GUEST / 'reports/windows-kmd-crosscheck.md').read_text()
        self.assertIn('从不给 22 个', report,
                      'the naming gap is no longer recorded in the crosscheck; '
                      're-derive the mapping before removing that caveat')


if __name__ == '__main__':
    unittest.main()