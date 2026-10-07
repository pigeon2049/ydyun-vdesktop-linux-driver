#!/usr/bin/env python3
"""Pin the centralized scene VA plan (r186): same values, single source."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'kernel' / 'mt_addr_plan.h'
CONSUMERS = [
    'kernel/recovery/mt_pvr_bridge.c',
    'kernel/recovery/mt_live_3d.c',
    'kernel/recovery/mt_live_3d_drm.c',
    'kernel/recovery/mt_live_drm.c',
    'kernel/recovery/mt_live_copy_bridge.c',
    'kernel/recovery/mt_live_tqx.c',
    'kernel/recovery/mt_live_tqx_repeat.c',
    'kernel/recovery/mt_live_tqx_readback.c',
]
# Macro -> the exact legacy literal it replaced (values must never change
# without a deliberate, documented round).
PINNED = {
    'MT_TQX_CMD_VA': '0x40000000ULL',
    'MT_TQX_DMA_VA': '0x40010000ULL',
    'MT_TQX_STATE_VA': '0x40020000ULL',
    'MT_TQX_CMD_BO_BYTES': '4096U',
    'MT_TQX_DMA_BO_BYTES': '8192U',
    'MT_TQX_STATE_BO_BYTES': '4096U',
    'MT_CTX_BO_BASE_VA': '0x50000000ULL',
    'MT_CTX_BO_STRIDE': '0x100000ULL',
    'MT_TRANSLATE_CMD_VA': '0x48000000ULL',
    'MT_TRANSLATE_CMD_BYTES': '32768U',
    'MT_TRANSLATE_SPACE_PAGES': '2112U',
    'MT_TRANSLATE_FENCE_WAIT_MS': '5000U',
    'MT_TRANSLATE_WAIT_SLICE_MS': '5U',
    'MT_TRANSFER_PROTO_W': '1280U',
    'MT_TRANSFER_PROTO_H': '1024U',
    'MT_TQX_STREAM_SRC_VA': '0x40100000ULL',
    'MT_TQX_STREAM_DST_VA': '0x40200000ULL',
    'MT_TQX_STREAM_SLOT_BYTES': '4096U',
    # r267: pre-seal scratch surface for live fire (8MB at 0x41000000).
    'MT_TQX_SCRATCH_VA': '0x41000000ULL',
    'MT_TQX_SCRATCH_BYTES': '8388608U',
}
# Raw scene literals that must not appear as code in consumers.
# Stream endpoints (0x40100000/0x40200000), slot_va bases and the unrelated
# dimension clamp live elsewhere and are out of scope by design.
BANNED = ['0x40000000', '0x40010000', '0x40020000', '0x50000000', '0x48000000']


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class AddrPlan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text()
        cls.sources = {p: (ROOT / p).read_text() for p in CONSUMERS}

    def test_header_pins_every_value(self):
        for name, literal in PINNED.items():
            self.assertRegex(
                self.header,
                r'#define\s+%s\s+%s\b' % (name, re.escape(literal)),
                '%s must keep its legacy value %s' % (name, literal))

    def test_consumers_include_the_plan(self):
        for path, text in self.sources.items():
            self.assertIn('#include "../mt_addr_plan.h"', text,
                          '%s must include the plan' % path)

    def test_no_raw_scene_literals_in_code(self):
        for path, text in self.sources.items():
            code = strip_comments(text)
            for lit in BANNED:
                self.assertIsNone(
                    re.search(re.escape(lit) + r'(?![0-9a-fA-F])', code),
                    '%s still hardcodes %s' % (path, lit))

    def test_bridge_aliases_come_from_macros(self):
        body = self.sources['kernel/recovery/mt_pvr_bridge.c']
        self.assertIn('tqx_va[3] = { MT_TQX_CMD_VA', body)
        self.assertIn('tqx_bytes[3] = { MT_TQX_CMD_BO_BYTES', body)
        self.assertIn('MT_CTX_BO_BASE_VA + i * MT_CTX_BO_STRIDE', body)

    def test_live_arrays_come_from_macros(self):
        for path in CONSUMERS[1:]:
            text = self.sources[path]
            if 'va[3]' in text or 'va[5]' in text:
                self.assertIn('MT_TQX_CMD_VA', text,
                              '%s array must use the triple macros' % path)
            if 'MT_GFX_CONTEXT_BO_COUNT' in text and 'MT_CTX_BO' not in text:
                self.fail('%s sizes context Bos but skips the base macro'
                          % path)


if __name__ == '__main__':
    unittest.main()
