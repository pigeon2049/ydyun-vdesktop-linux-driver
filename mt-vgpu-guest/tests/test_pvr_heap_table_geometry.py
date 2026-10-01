#!/usr/bin/env python3
"""Gate the heap table the UMD is served.

Two rules, both learned the hard way from the real UMD:

  1. Every published heap needs a non-zero base and size. The plan has two
     slots (4 and 5) that are empty on the reference side too. Publishing
     them as zero-length entries made the UMD build an arena with nothing to
     reserve; FUN_0019e7f0() returned 0 and RGXCreateDeviceMemContext failed
     with 82 = MTSRV_ERROR_DEVICEMEM_UNABLE_TO_CREATE_ARENA.

  2. Empty slots must be compacted out, not merely zeroed. The UMD walks
     indices 0..count-1 and builds one arena per entry, so "count = 11 with
     two holes" is not a table it can use.

These read the plan tables directly rather than the runtime table, so they
catch the geometry before a module is ever loaded.
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


if __name__ == '__main__':
    unittest.main()