#!/usr/bin/env python3
"""Pin the heap-name-to-slot mapping against the vendor heap blueprint.

The earlier bridge table published "PDS Code and Data" on a 0x8000 slot and
"USC Code" on a 0x1000 slot. The live UMD suballocates 0x9000 bytes from
MemHeap:PDS_CODE, which cannot fit in a 32 KiB heap, so RGXCreateRenderContext
failed with MTSRV_ERROR_DEVICEMEM_OUT_OF_DEVICE_VM.

The vendor's gasRGXHeapLayoutApp blueprint places those names on 4 GiB heaps:

    "PDS Code and Data"  base 0xda00000000  size 0x100000000
    "USC Code"           base 0xe000000000  size 0x100000000

The bridge must serve that blueprint rather than the eleven-entry guest
physical plan. These checks parse the table the bridge actually initializes.
"""
import re
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
WINDOWS_DECOMP = GUEST / 'decompiled/mtkm64.sys/decompiled.c'
QUEUE_H = GUEST / 'kernel/mt_pvr_queue.h'
BRIDGE_C = GUEST / 'kernel/recovery/mt_pvr_bridge.c'

EXPECTED_NAME_BY_SLOT = {
    0: 'General SVM',
    1: 'General',
    2: 'General NON-4K',
    3: 'PDS Code and Data',
    4: 'USC Code',
    5: 'Vulkan Capture Replay',
    6: 'Signals',
    7: 'Component Control',
    8: 'FBCDC',
    9: 'Large FBCDC',
    10: 'PDS Indirect State',
    11: 'Compute Mission RMW',
    12: 'Compute Safety RMW',
    13: 'Texture State',
    14: 'Visibility Test',
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


def blueprint_rows(text):
    """Parse the application blueprint served by the bridge."""
    m = re.search(
        r'static const struct mt_pvr_heap_entry app_heaps\[\]\s*=\s*\{(.*?)\};',
        text, re.S)
    if not m:
        raise AssertionError('app_heaps not found in mt_pvr_queue.h')
    return re.findall(
        r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)ULL\s*,\s*([0-9]+)ULL\s*,'
        r'\s*([0-9]+)ULL\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}',
        m.group(1))


class HeapNamingEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.queue = QUEUE_H.read_text()
        cls.bridge = BRIDGE_C.read_text()
        cls.win = strip_comments(WINDOWS_DECOMP.read_text()) \
            if WINDOWS_DECOMP.exists() else ''

    def test_bridge_serves_the_vendor_blueprint(self):
        self.assertIn(
            'mt_pvr_rgx_app_heaps_init(&file->heaps)', self.bridge,
            'the bridge is not initializing the vendor application heaps')
        self.assertNotIn(
            'mt_pvr_heaps_init(&file->heaps', self.bridge,
            'the bridge is still using the guest physical plan for PVR heaps')

    def test_pds_and_usc_are_on_four_gib_heaps(self):
        rows = {name: (int(base, 16), int(size))
                for name, base, size, _, _, _ in blueprint_rows(self.queue)}
        self.assertEqual(rows['PDS Code and Data'], (0xda00000000, 0x100000000))
        self.assertEqual(rows['USC Code'], (0xe000000000, 0x100000000))
        for name in ('PDS Code and Data', 'USC Code'):
            self.assertGreaterEqual(
                rows[name][1], UMD_PDS_SUBALLOC,
                f'{name} cannot hold the measured PDS suballocation')

    def test_every_blueprint_name_and_slot_matches(self):
        rows = blueprint_rows(self.queue)
        self.assertEqual(len(rows), 15)
        for slot, (name, _, _, _, _, _) in enumerate(rows):
            self.assertEqual(
                name, EXPECTED_NAME_BY_SLOT[slot],
                f'heap slot {slot} has the wrong name')

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
            'the Windows profile changed; recheck the distinction between '
            'named Windows resources and named PVR heaps')

    def test_crosscheck_still_records_the_naming_gap(self):
        report = GUEST / 'reports/windows-kmd-crosscheck.md'
        text = report.read_text()
        self.assertIn('从不给 22 个', text,
                      'the naming gap is no longer recorded; re-derive the '
                      'mapping before dropping that caveat')


if __name__ == '__main__':
    unittest.main()