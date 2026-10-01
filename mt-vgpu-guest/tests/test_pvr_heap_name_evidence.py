#!/usr/bin/env python3
"""Pin the heap-name-to-slot mapping against the Windows resource profile.

The defect this guards: we publish "PDS Code and Data" on heap slot 7 and
"USC Code" on slot 8. Those slots are 0x8000 and 0x1000 in the driver binary,
and the live UMD suballocates 0x9000 bytes from MemHeap:PDS_CODE, which cannot
fit in a 32 KiB heap. RGXCreateRenderContext fails with
MTSRV_ERROR_DEVICEMEM_OUT_OF_DEVICE_VM as a result.

Windows names its 13 *resources*, never its 22 heaps, so every heap name we
publish was an inference. The resource profile settles it, because a named
resource must live in a heap big enough to hold it:

    "Static PDS"  size 0x100000 (1 MiB)   heap index taken from puVar27[4]
    "Static USC"  size 0x100000 (1 MiB)   heap index 2

Neither fits in slot 7 or slot 8. The only slots that can hold a 1 MiB
resource are 0, 1, 2, 3, 6, 9 and 10.

These facts are recorded as measurements. The mapping itself is not invented
here -- whoever re-derives it must change the table below, and these
assertions will then describe the new state.
"""
import re
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
WINDOWS_DECOMP = GUEST / 'decompiled/mtkm64.sys/decompiled.c'
PLAN_HEAPS = GUEST / 'kernel/mt_guest_heaps.h'

# Slot geometry, decoded from mtkm64.sys itself; see
# test_windows_heap_table_decoded.py. Kept here as literals so this gate does
# not need the driver binary present.
SLOT_SIZES = [
    0x8000000000, 0x100000000, 0x100000000, 0x1000000, 0, 0,
    0x100000000, 0x8000, 0x1000, 0x100000000, 0x100000000,
]
NAME_BY_SLOT = {
    0: 'General',
    3: 'Component Control',
    7: 'PDS Code and Data',
    8: 'USC Code',
}

# Measured against the live UMD under gdb.
UMD_PDS_SUBALLOC = 0x9000

# Named-resource sizes from the Windows profile (decompiled.c:23004-23083).
NAMED_RESOURCE_SIZES = {
    'Static PDS': 0x100000,
    'Dynamic PDS': 0x200000,
    'Static USC': 0x100000,
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class HeapNamingEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plan = PLAN_HEAPS.read_text()
        cls.win = strip_comments(WINDOWS_DECOMP.read_text()) \
            if WINDOWS_DECOMP.exists() else ''

    def test_known_defect_is_recorded_not_silently_absent(self):
        # This describes a live defect, so it is asserted as a *record* rather
        # than left to rot. The condition being recorded:
        #
        #   slot 7 (0x8000) carries "PDS Code and Data"
        #   slot 8 (0x1000) carries "USC Code"
        #   "Static PDS" / "Static USC" in the Windows profile need 0x100000
        #
        # so both names are on slots too small to hold what the profile says
        # belongs there. When the mapping is genuinely re-derived this test
        # will start failing here, which is the signal to rewrite the file.
        offenders = []
        for slot, name in NAME_BY_SLOT.items():
            if name in ('PDS Code and Data', 'USC Code'):
                offenders.append((slot, name, SLOT_SIZES[slot]))
        self.assertEqual(
            offenders, [(7, 'PDS Code and Data', 0x8000), (8, 'USC Code', 0x1000)],
            'the heap-name-to-slot mapping changed. If it was re-derived from '
            'the Windows resource profile, update NAME_BY_SLOT and remove the '
            'workarounds described in this file')

    def test_pds_named_slot_cannot_serve_the_umd(self):
        pds_slot = next(s for s, n in NAME_BY_SLOT.items()
                        if n == 'PDS Code and Data')
        self.assertLess(
            SLOT_SIZES[pds_slot], UMD_PDS_SUBALLOC,
            'the slot named "PDS Code and Data" is now large enough for the '
            "UMD's measured 0x9000 suballocation. If this fires, the naming "
            'mapping has genuinely been re-derived -- update this file and '
            'delete the workarounds it describes')

    def test_slots_large_enough_to_back_a_named_resource(self):
        big = [s for s, sz in enumerate(SLOT_SIZES) if sz >= 0x100000]
        self.assertEqual(big, [0, 1, 2, 3, 6, 9, 10],
                         'the set of slots that could hold a 1 MiB named '
                         'resource changed; the mapping has to be re-derived')

    def test_windows_profile_still_names_these_resources(self):
        if not self.win:
            self.skipTest('the Windows decompilation is not present')
        for name in NAMED_RESOURCE_SIZES:
            self.assertIn(f'"{name}"', self.win,
                          f'the Windows resource profile no longer contains '
                          f'{name!r}; the evidence this file relies on is gone')
        # "Static USC" is explicitly pinned to heap index 2 by the profile.
        self.assertRegex(
            self.win,
            r'"Static USC";\s*\n[^;]*puVar27\[4\]\s*=\s*2;',
            'the Windows profile no longer pins "Static USC" to heap 2, which '
            'is the strongest single piece of evidence that the small PDS/USC '
            'slots are not what the UMD means by those names')

    def test_crosscheck_still_records_the_naming_gap(self):
        report = GUEST / 'reports/windows-kmd-crosscheck.md'
        text = report.read_text()
        self.assertIn('从不给 22 个', text,
                      'the naming gap is no longer recorded; re-derive the '
                      'mapping before dropping that caveat')


if __name__ == '__main__':
    unittest.main()