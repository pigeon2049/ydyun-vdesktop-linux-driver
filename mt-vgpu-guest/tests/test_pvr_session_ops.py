#!/usr/bin/env python3
"""Gate the session side of the DMA contract (mt_guest_probe exports).

The bridge degrades gracefully when these are absent, but once present they
must obey the contract in kernel/mt_pvr_session.h: versioned table, validated
acquisition, unwound partial failures, and a loud remove path. These read the
probe source the same way the lifetime gates read the bridge source.
"""
import re
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
PROBE = GUEST / 'kernel/mt_guest_probe.c'
HEADER = GUEST / 'kernel/mt_pvr_session.h'


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


class SessionOps(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(PROBE.read_text())
        cls.header = HEADER.read_text()
        cls.get = function_body('mt_pvr_session_get', cls.text)
        cls.put = function_body('mt_pvr_session_put', cls.text)
        cls.map = function_body('mt_pvr_session_dma_map', cls.text)
        cls.unmap = function_body('mt_pvr_session_dma_unmap', cls.text)

    def test_table_is_exported_with_version(self):
        self.assertIn('EXPORT_SYMBOL(mt_pvr_session_ops)', self.text,
                      'the bridge resolves the table by name; no export, '
                      'no handoff')
        self.assertRegex(
            self.text,
            r'mt_pvr_session_ops\s*=\s*\{[^}]*\.abi_version\s*=\s*'
            r'MT_PVR_SESSION_ABI_VERSION',
            'the exported table must carry the header version')

    def test_acquire_validates_binding_and_liveness(self):
        # Stale drvdata or a dead trial must degrade the caller, never hand
        # out a session whose mappings mean nothing.
        self.assertIn('device_lock', self.get)
        self.assertIn('device_unlock', self.get)
        self.assertRegex(self.get, r'strcmp\(pdev->driver->name,\s*"mt_guest_probe"\)')
        self.assertIn('pci_get_drvdata(pdev)', self.get)
        self.assertIn('g->trial.pinned', self.get)
        self.assertIn('g->trial.connected', self.get)
        self.assertIn('try_module_get', self.get,
                      'no module pin: unload could free the session mid-call')

    def test_map_unwinds_partial_failures(self):
        # A failed page N must unmap pages 0..N-1 and must NOT count the
        # mapping as live. Otherwise remove() believes mappings exist that
        # were never completed, or leaks real ones.
        self.assertIn('dma_unmap_page', self.map,
                      'partial mapping failures leak DMA mappings')
        self.assertEqual(self.map.count('atomic_inc('), 1,
                         'the live-maps counter must increment exactly once, '
                         'on full success only')

    def test_unmap_counts_down(self):
        self.assertIn('atomic_dec(', self.unmap,
                      'unmap must release the live-maps count')

    def test_remove_warns_on_live_mappings(self):
        remove = function_body('mt_remove', self.text)
        self.assertIn('atomic_read(&mt_pvr_live_maps)', remove,
                      'unbinding under live bridge mappings must be loud')

    def test_bind_sets_explicit_dma_mask(self):
        # The running device inherited 40 bits from the vendor driver, but a
        # first-bind by this driver would fall back to the default and fail
        # every dma_map_page in the session DMA service. Caught by reading
        # live dma_mask_bits, not by any test failure.
        probe = function_body('mt_probe', self.text)
        self.assertIn('dma_set_mask_and_coherent', probe,
                      'bind path never sets the DMA mask the session '
                      'service depends on')
        self.assertIn('DMA_BIT_MASK(40)', probe)

    def test_pages_cross_as_struct_page_not_addresses(self):
        # virt_to_page() is invalid on vmalloc addresses and the bridge backs
        # PMRs with vzalloc, so the page itself crosses the contract. A
        # page_address()/virt_to_page() round trip computes garbage pages and
        # would DMA-map arbitrary memory. This bug class was caught in review
        # before first load, never on hardware.
        bridge = (GUEST / 'kernel/recovery/mt_pvr_bridge.c').read_text()
        bridge = strip_comments(bridge)
        register = function_body('pvr_pmr_dma_register', bridge)
        self.assertIn('pages[i] = page;', register,
                      'bridge must pass struct page *, not an address')
        self.assertNotIn('page_address(page)', register,
                         'page_address round trip breaks on vmalloc pages')
        self.assertIn('(struct page *)cpu_pages[i]', self.map,
                      'session must take struct page * directly')
        self.assertNotIn('virt_to_page', self.map,
                         'virt_to_page on vmalloc addresses yields garbage')

    def test_direction_is_explicitly_deferred(self):
        # Bidirectional-until-learned is a deliberate choice, not an
        # oversight; the (void)dir marks say so.
        self.assertIn('(void)dir', self.map)
        self.assertIn('(void)dir', self.unmap)


if __name__ == '__main__':
    unittest.main()
