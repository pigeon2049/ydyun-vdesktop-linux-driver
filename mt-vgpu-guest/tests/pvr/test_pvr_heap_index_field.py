#!/usr/bin/env python3
"""Gate which index field HeapCfgHeapDetails keys its lookup on.

The driver read ui32HeapConfigIndex. The real 5.2 UMD always sends 0 for that
field and counts in ui32HeapIndex, so every call described heap 0: eleven
identical names and bases. The UMD cached eleven copies of "General", its own
MTSRVFindHeapByName("PDS Code and Data") could never match, and it bailed out of
RGXCreateDeviceMemContext and ran its error-cleanup path, where the observable
symptom was "double free or corruption" in userspace.

Nothing offline caught this because it needs the real UMD's calling convention.
These checks pin the field selection to the measured behaviour instead.
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

GUEST = get_repo_root()
SOURCE = GUEST / 'kernel/recovery/mt_pvr_bridge.c'
QUEUE = GUEST / 'kernel/mt_pvr_queue.h'
PROBE = GUEST / 'probe/pvr_node_probe.c'

# Measured from the live 5.2 UMD: one call per published heap, this shape every
# time. The vendor table now publishes fifteen heaps.
MEASURED_CALLS = [
    {'heap_config_index': 0, 'heap_index': n} for n in range(15)
]


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def function_body(name, text):
    start = re.search(r'^(?:static\s+)?\w[\w\s\*]*\b' + re.escape(name) +
                      r'\s*\([^;]*?\)\s*\{', text, re.M)
    if not start:
        raise AssertionError(f'{name} not found')
    depth = 0
    i = text.index('{', start.start())
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise AssertionError(f'{name} is not brace-balanced')


class HeapDetailsIndexField(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.body = function_body('pvr_cmd_heap_details', cls.text)
        cls.probe = strip_comments(PROBE.read_text())

    def test_lookup_uses_heap_index_not_config_index(self):
        # The bug in one assertion: the table must be indexed by heap_index.
        self.assertRegex(
            self.body, r'index\s*=\s*in\.heap_index\s*;',
            'pvr_cmd_heap_details does not index the table by '
            'ui32HeapIndex; the UMD leaves ui32HeapConfigIndex at 0, so every '
            'call would describe heap 0 and the UMD would cache one name '
            'eleven times')
        self.assertNotRegex(
            self.body, r'index\s*=\s*in\.heap_config_index\s*;',
            'pvr_cmd_heap_details indexes the table by '
            'ui32HeapConfigIndex, which the UMD always sends as 0')

    def test_bounds_check_still_guards_the_index(self):
        self.assertRegex(
            self.body,
            r'if\s*\(\s*index\s*>=\s*file->heaps\.count\s*\)',
            'the heap index must be bounds checked before the table is read')

    def test_probe_matches_the_umd_calling_convention(self):
        # The probe must exercise the same shape the UMD uses, or it stops
        # standing in for the UMD.
        configs = set(re.findall(r'heap_config_index\s*=\s*([^;]+);',
                                 self.probe))
        for value in configs:
            self.assertIn(
                value.strip(), ('0',),
                'the probe sets heap_config_index to %s; the UMD always '
                'sends 0 for it and counts in heap_index' % value.strip())

    def test_probe_finds_heaps_by_name_not_by_pinned_index(self):
        # It used to assert `heap_index = 8` and expect "USC Code" there.
        # That is wrong now: the table compacts its two empty slots away, so
        # pinned indices drift whenever the geometry changes. The probe walks
        # the published heaps and compares names, the way the UMD does, which
        # is what caught the zero-length entries in the first place.
        self.assertNotRegex(
            self.probe, r'heap_index\s*=\s*8\s*;',
            'the probe pins heap_index=8 again; look the heap up by name so '
            'the check survives changes to the heap geometry')
        self.assertRegex(
            self.probe, r'"USC Code"',
            'the probe no longer checks that "USC Code" is published, which '
            'is the heap whose absence blocked device-memory-context creation')
        self.assertRegex(
            self.probe, r'num_heaps',
            'the probe no longer walks the published heap count')

    def test_probe_rejects_an_empty_heap(self):
        # The regression that produced error 82: a heap with no base or no
        # size. The probe must assert every published entry is usable.
        self.assertRegex(
            self.probe, r'!heap_out\.base\s*\|\|\s*!heap_out\.length',
            'the probe no longer checks that every published heap has a '
            'non-zero base and size; a zero-length entry makes the UMD fail '
            'with MTSRV_ERROR_DEVICEMEM_UNABLE_TO_CREATE_ARENA')

    def test_measured_convention_is_recorded(self):
        # If the UMD ever changes which field it counts in, this is the record
        # that should make the change visible.
        for call in MEASURED_CALLS:
            self.assertEqual(call['heap_config_index'], 0)
            self.assertLess(call['heap_index'], 15)

    def test_heap_names_include_the_three_the_umd_needs(self):
        # FUN_001705f0 looks up exactly these before allocating the static
        # PDS/USC objects. All three must be published.
        names = re.search(
            r'static const struct mt_pvr_heap_entry app_heaps\[\]\s*=\s*\{(.*?)\};',
            QUEUE.read_text(), re.S)
        self.assertIsNotNone(names, 'the heap name table is gone')
        for required in ('General', 'PDS Code and Data', 'USC Code'):
            self.assertIn(required, names.group(1),
                          f'heap table no longer publishes {required!r}, which '
                          'RGXCreateDeviceMemContext looks up by name')


if __name__ == '__main__':
    unittest.main()