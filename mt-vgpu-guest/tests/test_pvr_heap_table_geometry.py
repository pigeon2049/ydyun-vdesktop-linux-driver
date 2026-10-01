#!/usr/bin/env python3
"""Gate the heap table the UMD is served.

The bridge serves the vendor's fifteen-entry RGX application blueprint. The
older eleven-entry guest physical plan is still used for guest-side VM
resources and is covered below only as a compatibility helper. Two rules, both
learned the hard way from the real UMD:

  1. Every published heap needs a non-zero base and size. Publishing a
     zero-length entry made the UMD build an arena with nothing to reserve;
     FUN_0019e7f0() returned 0 and RGXCreateDeviceMemContext failed with 82 =
     MTSRV_ERROR_DEVICEMEM_UNABLE_TO_CREATE_ARENA.

  2. PDS and USC must resolve to heaps large enough for the UMD's measured
     0x9000-byte PDS suballocation. The earlier table put those names on
     0x8000 and 0x1000 slots; the vendor blueprint puts them on 4 GiB heaps.

These read the tables directly rather than the runtime table, so they catch
the geometry before a module is ever loaded.
"""
import re
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
HEAPS_H = GUEST / 'kernel/mt_guest_heaps.h'
QUEUE_H = GUEST / 'kernel/mt_pvr_queue.h'


def c_array(text, name):
    """Parse a `static const <type> name[] = { ... };` initializer."""
    m = re.search(r'\b' + re.escape(name) + r'\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\}',
                  text, re.S)
    if not m:
        raise AssertionError(f'{name} not found in {HEAPS_H}')
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return [v.strip() for v in body.split(',') if v.strip()]


def app_blueprint_rows(text):
    """Parse the vendor RGX application blueprint in mt_pvr_queue.h."""
    m = re.search(
        r'static const struct mt_pvr_heap_entry app_heaps\[\]\s*=\s*\{(.*?)\};',
        text, re.S)
    if not m:
        raise AssertionError('app_heaps not found in mt_pvr_queue.h')
    rows = re.findall(
        r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)ULL\s*,\s*([0-9]+)ULL\s*,'
        r'\s*([0-9]+)ULL\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}',
        m.group(1))
    if not rows:
        raise AssertionError('app_heaps has no parseable blueprint rows')
    return [(name, int(base, 16), int(size), int(reserved), int(log2), int(align))
            for name, base, size, reserved, log2, align in rows]


EXPECTED_APP_BLUEPRINT = [
    ("General SVM", 0x4000000000, 274877906944, 2097152, 0, 0),
    ("General", 0x8000000000, 137438953472, 65536, 0, 0),
    ("General NON-4K", 0xb800000000, 34359738368, 0, 0, 0),
    ("PDS Code and Data", 0xda00000000, 4294967296, 65536, 0, 0),
    ("USC Code", 0xe000000000, 4294967296, 65536, 0, 0),
    ("Vulkan Capture Replay", 0xe900000000, 1073741824, 0, 0, 0),
    ("Signals", 0xea00000000, 65536, 0, 0, 0),
    ("Component Control", 0xeb00000000, 4294967296, 0, 0, 0),
    ("FBCDC", 0xec00000000, 2097152, 0, 0, 0),
    ("Large FBCDC", 0xec40000000, 2097152, 0, 0, 0),
    ("PDS Indirect State", 0xed00000000, 16777216, 0, 0, 0),
    ("Compute Mission RMW", 0xee00000000, 1073741824, 0, 0, 0),
    ("Compute Safety RMW", 0xef00000000, 1073741824, 0, 0, 0),
    ("Texture State", 0xf000000000, 4294967296, 0, 0, 0),
    ("Visibility Test", 0xf200000000, 2097152, 0, 0, 0),
]


class HeapTableGeometry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.heaps = HEAPS_H.read_text()
        cls.queue = QUEUE_H.read_text()

    def test_plan_has_empty_slots(self):
        # Recorded, not enforced: if a future plan fills slots 4 and 5 this
        # should fail loudly so the expectations below can be revisited.
        bases = c_array(self.heaps, 'bases')
        sizes = c_array(self.heaps, 'sizes')
        empty = [i for i, (b, s) in enumerate(zip(bases, sizes))
                 if b in ('0', '0ULL', '0U') or s in ('0', '0ULL', '0U')]
        self.assertEqual(
            empty, [4, 5],
            'the plan\'s empty slots moved from indices 4 and 5 to %s; the '
            'compaction logic and the expected heap count both need updating'
            % empty)

    def test_expected_heap_count(self):
        bases = c_array(self.heaps, 'bases')
        sizes = c_array(self.heaps, 'sizes')
        live = [(b, s) for b, s in zip(bases, sizes)
                if b not in ('0', '0ULL', '0U') and s not in ('0', '0ULL', '0U')]
        self.assertEqual(
            len(live), 9,
            'expected 9 populated heaps after compaction, found %d; the UMD '
            'was measured building exactly 9 arenas' % len(live))

    def test_init_compacts_empty_slots(self):
        body = re.search(
            r'static inline void mt_pvr_heaps_init\(.*?\n\}',
            self.queue, re.S)
        self.assertIsNotNone(body, 'mt_pvr_heaps_init is gone')
        text = body.group(0)
        self.assertRegex(
            text, r'if\s*\(\s*!base\s*\|\|\s*!size\s*\)\s*\n\s*continue',
            'mt_pvr_heaps_init does not skip heaps with no base or size; the '
            'UMD will try to build an arena for each entry and fail on a '
            'zero-length one')
        # The count must be derived from what was actually stored, not from
        # the fixed table width.
        self.assertRegex(
            text, r'table->count\+\+',
            'mt_pvr_heaps_init still assigns a fixed count instead of counting '
            'the heaps it stored')
        self.assertNotRegex(
            text, r'table->count\s*=\s*MT_PVR_HEAP_COUNT',
            'mt_pvr_heaps_init reports the fixed table width as the heap '
            'count, which reintroduces the empty slots the UMD chokes on')

    def test_names_travel_with_their_entry(self):
        # Compaction must not orphan the names, or the UMD's by-name lookups
        # break even though the geometry is right.
        body = re.search(
            r'static inline void mt_pvr_heaps_init\(.*?\n\}',
            self.queue, re.S).group(0)
        self.assertRegex(
            body, r'entries\[table->count\]\.name\s*=\s*names\s*\?\s*names\[i\]',
            'the name must be stored into the same compacted slot as the base '
            'and size, or the UMD looks up names against shifted entries')

    def test_heaps_init_never_publishes_a_zero_sized_heap(self):
        # The invariant, stated directly: nothing stored may be empty.
        self.assertRegex(
            self.queue, r'if\s*\(\s*!base\s*\|\|\s*!size\s*\)\s*\n\s*continue',
            'no guard against publishing an empty heap entry exists')

    def test_bridge_serves_the_vendor_application_blueprint(self):
        rows = app_blueprint_rows(self.queue)
        self.assertEqual(
            rows, EXPECTED_APP_BLUEPRINT,
            'the bridge heap blueprint drifted from the vendor table')
        for name, base, size, _, _, _ in rows:
            self.assertTrue(name)
            self.assertNotEqual(base, 0)
            self.assertNotEqual(size, 0)

    def test_pds_and_usc_resolve_to_four_gib_heaps(self):
        rows = dict((name, (base, size))
                    for name, base, size, _, _, _ in app_blueprint_rows(self.queue))
        for name in ('PDS Code and Data', 'USC Code'):
            base, size = rows[name]
            self.assertGreaterEqual(
                size, 0x9000,
                f'{name} cannot hold the measured PDS suballocation')
        self.assertEqual(rows['PDS Code and Data'][0], 0xda00000000)
        self.assertEqual(rows['USC Code'][0], 0xe000000000)


if __name__ == '__main__':
    unittest.main()
