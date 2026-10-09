#!/usr/bin/env python3
"""Gate the production TA completion path (r370).

r368 diagnosed the structural hang: the frozen probe's mt_runtime_event()
routes DM3 events to the generic mt_marker_complete(), which rejects the
TA completion code 0x100 (MT_FW_TA_COMPLETE_CODE). With zero production
callers of mt_marker_complete_ta(), TA markers hung forever (wire 6).

r370 fixes this in the bridge (the probe is frozen): after a successful
0x82:0xC TA dispatch, pvr_cmd_musakickgfx2() polls the DM3 event ring for
the 0x100 completion (pvr_ta_wait_complete) and retires the marker via
mt_marker_complete_ta(); on timeout it error-signals via pvr_ta_abandon()
so check_fence waiters never hang forever.

This test statically verifies:
 1. pvr_ta_wait_complete() exists and polls for MT_FW_TA_COMPLETE_CODE,
    calling mt_marker_complete_ta() on a wire_id match.
 2. pvr_ta_abandon() exists and error-signals the fence on timeout.
 3. The dispatch success path calls pvr_ta_wait_complete() after submit.

Removing the wait call (reverting to the r368 hang) must turn this
test red (reverse validation).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRIDGE = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def extract_function(src, name, rettype="static int"):
    m = re.search(r'%s %s\([^)]*\)\s*\{' % (re.escape(rettype),
                                            re.escape(name)), src)
    assert m, 'function %s not found' % name
    start = m.end() - 1
    depth = 0
    for i in range(start, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    raise AssertionError('unbalanced braces in %s' % name)


class TestTaCompletionPath(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = BRIDGE.read_text(encoding='utf-8')
        cls.wait = extract_function(cls.src, 'pvr_ta_wait_complete')
        cls.abandon = extract_function(cls.src, 'pvr_ta_abandon',
                                       rettype="static void")
        cls.dispatch = extract_function(cls.src, 'pvr_cmd_musakickgfx2')

    def test_wait_polls_for_ta_complete_code(self):
        self.assertIn('MT_FW_TA_COMPLETE_CODE', self.wait,
                      'wait must match the TA completion code 0x100')
        self.assertIn('mt_marker_complete_ta', self.wait,
                      'wait must retire via the TA-aware completion path')

    def test_wait_matches_wire_id(self):
        self.assertIn('wire_id', self.wait,
                      'wait must match the completion event by wire_id')

    def test_wait_has_timeout(self):
        self.assertIn('ETIMEDOUT', self.wait,
                      'wait must time out instead of hanging forever')

    def test_abandon_error_signals_fence(self):
        self.assertIn('dma_fence_set_error', self.abandon,
                      'abandon must error-signal the fence')
        self.assertIn('dma_fence_signal', self.abandon,
                      'abandon must signal the fence')

    def test_dispatch_waits_for_completion(self):
        # Success path: after dma_fence_put(fence), before out.error = 0,
        # the dispatch must wait for TA completion (r370).
        # (r398: kfree(ctx) removed; ctx is borrowed from render_ctx.)
        m = re.search(r'dma_fence_put\(fence\);\s*(.*?)\s*out\.error\s*=\s*0;',
                      self.dispatch, re.S)
        self.assertIsNotNone(m, 'dispatch success path region not found')
        self.assertIn('pvr_ta_wait_complete', m.group(1),
                      'dispatch must wait for TA completion after submit '
                      '(else the marker hangs like r368 wire 6)')

    def test_dispatch_handles_timeout(self):
        # (r398: kfree(ctx) removed; ctx is borrowed from render_ctx.)
        m = re.search(r'dma_fence_put\(fence\);\s*(.*?)\s*out\.error\s*=\s*0;',
                      self.dispatch, re.S)
        self.assertIsNotNone(m, 'dispatch success path region not found')
        self.assertIn('pvr_ta_abandon', m.group(1),
                      'dispatch must abandon on completion timeout')


if __name__ == '__main__':
    unittest.main()
