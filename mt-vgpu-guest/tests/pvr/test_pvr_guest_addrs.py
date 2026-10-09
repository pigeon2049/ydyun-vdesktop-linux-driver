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
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
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

    def test_match_helper_pinned(self):
        self.assertRegex(
            self.wire,
            r'vendor == 0x1ed5 && device == 0x0222 &&\s*'
            r'subvendor == 0x1ed5 && subdevice == 0x1101',
            'match helper must keep the S3000 quad')
        self.assertRegex(
            self.wire,
            r'#define\s+MT_GUEST_DRIVER_NAME\s+"mt_guest_probe"',
            'driver name must keep its value')
        self.assertRegex(
            self.wire,
            r'pci_get_domain_bus_and_slot\(0, 0, PCI_DEVFN\(14, 0\)\)',
            'slot lookup helper must keep slot 14')

    def test_guards_call_helper(self):
        # Seventeen recovery files share the full-quad guard; the loose
        # matches (drm_snapshot, irq_recover, live_service, the bridge
        # device lookup, device_profile) are different semantics.
        guarded = [
            'kernel/recovery/mt_cold_disconnect.c',
            'kernel/recovery/mt_retired_disconnect.c',
            'kernel/recovery/mt_idle_disconnect.c',
            'kernel/recovery/mt_drain_pending.c',
            'kernel/recovery/mt_hwr_request.c',
            'kernel/recovery/mt_master_notify.c',
            'kernel/recovery/mt_package_probe.c',
            'kernel/recovery/mt_retained_dump.c',
            'kernel/recovery/mt_reconnect.c',
            'kernel/recovery/mt_offline_info.c',
            'kernel/recovery/mt_live_3d.c',
            'kernel/recovery/mt_live_copy_bridge.c',
            'kernel/recovery/mt_live_drm.c',
            'kernel/recovery/mt_live_tqx.c',
            'kernel/recovery/mt_live_tqx_repeat.c',
            'kernel/recovery/mt_live_marker.c',
            'kernel/recovery/mt_live_3d_drm.c',
        ]
        for rel in guarded:
            text = strip_comments((ROOT / rel).read_text())
            self.assertIn('mt_guest_match_s3000(', text,
                          '%s must call the helper' % rel)
            self.assertNotRegex(text, r'vendor != 0x1ed5',
                                '%s must not carry a bare quad' % rel)
        slot_guarded = [
            'kernel/recovery/mt_cold_disconnect.c',
            'kernel/recovery/mt_retired_disconnect.c',
            'kernel/recovery/mt_idle_disconnect.c',
            'kernel/recovery/mt_drain_pending.c',
            'kernel/recovery/mt_hwr_request.c',
            'kernel/recovery/mt_master_notify.c',
            'kernel/recovery/mt_package_probe.c',
            'kernel/recovery/mt_retained_dump.c',
            'kernel/recovery/mt_reconnect.c',
            'kernel/recovery/mt_offline_info.c',
            'kernel/recovery/mt_live_3d.c',
            'kernel/recovery/mt_live_copy_bridge.c',
            'kernel/recovery/mt_live_drm.c',
            'kernel/recovery/mt_live_tqx.c',
            'kernel/recovery/mt_live_tqx_repeat.c',
            'kernel/recovery/mt_live_marker.c',
            'kernel/recovery/mt_live_3d_drm.c',
            'kernel/recovery/mt_live_tqx_readback.c',
            'kernel/recovery/mt_drm_snapshot.c',
            'kernel/recovery/mt_tqx_snapshot.c',
            'kernel/recovery/mt_fix_poll.c',
            'kernel/recovery/mt_irq_recover.c',
            'kernel/recovery/mt_live_service.c',
        ]
        for rel in slot_guarded:
            text = strip_comments((ROOT / rel).read_text())
            self.assertIn('mt_guest_find_s3000()', text,
                          '%s must call the slot helper' % rel)
            self.assertNotIn('PCI_DEVFN(14, 0))', text,
                             '%s must not carry a bare slot' % rel)
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
