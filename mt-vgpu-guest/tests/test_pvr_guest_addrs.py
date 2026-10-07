#!/usr/bin/env python3
"""Pin guest-visible PCI/segment addresses to one macro each (r269).

BAR2 base/size and the info segment-5 address were pasted literals in
four recovery files. Values come from lspci (16G prefetchable window)
and decode-device-info.py (version-2 info layout); the vendor heap
table itself stays literal-by-design in mt_guest_heaps.h (pinned by
test_windows_heap_table_decoded.py), and the 1GiB
vm_memory_size_bytes quota is decoded, never consumed in-kernel.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIRE = ROOT / 'kernel' / 'mt_guest_device.h'
FILES = [
    'kernel/recovery/mt_cold_disconnect.c',
    'kernel/recovery/mt_retired_disconnect.c',
    'kernel/recovery/mt_idle_disconnect.c',
    'kernel/recovery/mt_offline_info.c',
]

PINNED = {
    'MT_GUEST_BAR2_BASE': '0x800000000ULL',
    'MT_GUEST_BAR2_BYTES': '0x400000000ULL',
    'MT_GUEST_SEG5_ADDR': '0x43000000ULL',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class GuestAddrs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wire = WIRE.read_text()

    def test_wire_pins_each_value(self):
        for name, literal in PINNED.items():
            self.assertRegex(
                self.wire,
                r'#define\s+%s\s+%s\b' % (name, re.escape(literal)),
                '%s must keep its value %s' % (name, literal))

    def test_recovery_compares_by_name(self):
        # BAR2 base/size live in the three disconnect guards; the
        # segment-5 anchor lives in the offline info layout checks.
        expected = {
            'kernel/recovery/mt_cold_disconnect.c': [
                'MT_GUEST_BAR2_BASE', 'MT_GUEST_BAR2_BYTES'],
            'kernel/recovery/mt_retired_disconnect.c': [
                'MT_GUEST_BAR2_BASE', 'MT_GUEST_BAR2_BYTES'],
            'kernel/recovery/mt_idle_disconnect.c': [
                'MT_GUEST_BAR2_BASE', 'MT_GUEST_BAR2_BYTES'],
            'kernel/recovery/mt_offline_info.c': ['MT_GUEST_SEG5_ADDR'],
        }
        for rel, names in expected.items():
            text = strip_comments((ROOT / rel).read_text())
            for name in names:
                self.assertIn(name, text,
                              '%s must use %s' % (rel, name))
        bare = {
            'kernel/recovery/mt_cold_disconnect.c': [
                '0x800000000ULL', '0x400000000ULL'],
            'kernel/recovery/mt_retired_disconnect.c': [
                '0x800000000ULL', '0x400000000ULL'],
            'kernel/recovery/mt_idle_disconnect.c': [
                '0x800000000ULL', '0x400000000ULL'],
            'kernel/recovery/mt_offline_info.c': ['0x43000000'],
        }
        for rel, literals in bare.items():
            text = strip_comments((ROOT / rel).read_text())
            for literal in literals:
                self.assertNotIn(literal, text,
                                 '%s must not carry bare %s'
                                 % (rel, literal))


if __name__ == '__main__':
    unittest.main()
