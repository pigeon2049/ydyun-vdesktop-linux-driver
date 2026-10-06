#!/usr/bin/env python3
"""Gate the session-side prerequisites for DMA (mt_guest_probe bind path).

The bridge maps PMR pages with core dma_map_page() against the live session's
device, which needs two things from the session side, both established here:

  1. An explicit DMA mask at bind. The running device reports 40 bits only
     because the vendor driver left it behind; a first-bind by this driver
     would fall back to the default and fail every mapping.
  2. No symbol machinery. Cross-module symbol_get() does not resolve on this
     kernel (verified empirically, even for printk), so the probe exports
     nothing and the bridge acquires through PCI lookup + drvdata + module
     ref. Any reintroduction would silently break the handoff.
"""
import re
import unittest
from pathlib import Path

GUEST = Path(__file__).resolve().parents[1]
PROBE = GUEST / 'kernel/mt_guest_probe.c'
BRIDGE = GUEST / 'kernel/recovery/mt_pvr_bridge.c'


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


class SessionPrereqs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(PROBE.read_text())

    def test_bind_sets_explicit_dma_mask(self):
        probe = function_body('mt_probe', self.text)
        self.assertIn('dma_set_mask_and_coherent', probe,
                      'bind path never sets the DMA mask the session '
                      'DMA mappings depend on')
        self.assertIn('DMA_BIT_MASK(40)', probe)

    def test_no_symbol_machinery_in_probe(self):
        for name in ('symbol_get', 'symbol_put', 'EXPORT_SYMBOL(mt_pvr_session',
                     'mt_pvr_session_ops'):
            self.assertNotIn(name, self.text,
                             f'{name} must not appear: symbol resolution '
                             'does not work on this kernel')

    def test_bridge_lookup_uses_single_slot_macro(self):
        bridge = strip_comments(BRIDGE.read_text())
        self.assertRegex(
            bridge,
            r'#define\s+MT_PVR_PCI_DEVFN\s+PCI_DEVFN\(14,\s*0\)',
            'the S3000 slot assumption must live in exactly one macro')
        self.assertEqual(
            bridge.count('PCI_DEVFN(14, 0)'), 1,
            'the S3000 slot literal may appear exactly once (inside the '
            'MT_PVR_PCI_DEVFN definition); lookups must use the macro')
        self.assertGreaterEqual(
            bridge.count('MT_PVR_PCI_DEVFN'), 3,
            'macro definition plus every lookup site must reference it')


if __name__ == '__main__':
    unittest.main()
