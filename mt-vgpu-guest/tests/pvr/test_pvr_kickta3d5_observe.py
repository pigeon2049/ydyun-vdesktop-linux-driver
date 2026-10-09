#!/usr/bin/env python3
"""Gate the RGXKickTA3D5 accept-and-log handler (r215).

0x82:0x14 must reach pvr_cmd_kickta3d5_observe, which reports the named
CCB window plus scalar check/update/sync-PMR counts and returns 0 without
executing anything: bounded scan, render-context handle validated, nested
check/update/sync-PMR pointer arrays untouched, no firmware/DMA/
translator path reachable. Mirrors the 0x89:0xa observer (r174).
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

SOURCE = get_repo_root() / 'kernel/recovery/mt_pvr_bridge.c'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def fn_body(src, name):
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)


class KickTA3D5Observe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        m = re.search(r'case MT_PVR_BRIDGE_RGXTA3D:(.*?)case MT_PVR_BRIDGE_\w+:',
                      cls.src, re.S)
        assert m, 'RGXTA3D dispatch block not found'
        cls.dispatch = m.group(1)
        cls.body = fn_body(cls.src, 'pvr_cmd_kickta3d5_observe')

    def test_kickta3d5_routed(self):
        self.assertRegex(self.dispatch,
                         r'case MT_PVR_FN_RGXKICKTA3D5:[\s\S]*?pvr_cmd_kickta3d5_observe',
                         '0x82:0x14 must reach the observe handler, not -ENOTTY')

    def test_window_bounded(self):
        self.assertIn('MT_PVR_SUBMIT3_LOG_MAX', self.body)
        self.assertIn('submission_size > MT_PVR_SUBMIT3_LOG_MAX', self.body)

    def test_context_validated(self):
        self.assertIn('pvr_object_find(', self.body)
        self.assertIn('MT_PVR_KIND_CONTEXT', self.body)
        self.assertIn('-ENOENT', self.body)

    def test_nested_arrays_untouched(self):
        for field in ('check_sync_prim_blocks', 'check_sync_offsets',
                      'check_values', 'update_sync_prim_blocks',
                      'update_sync_offsets', 'update_values',
                      'sync_pmr_flags', 'sync_pmrs'):
            self.assertNotIn(field, self.body,
                             'observe path must not read nested %s' % field)

    def test_counts_reported_not_consumed(self):
        for field in ('check_count', 'update_count', 'sync_pmr_count',
                      'submission_flags', 'submission_id'):
            self.assertIn(field, self.body,
                          'observe path must log scalar %s' % field)

    def test_no_execution_path(self):
        for token in ('dma_submit', 'mt_fw_event',
                      'mt_system_submit', 'doorbell', 'submit_tqx_work',
                      'submit_context', 'dma_fence',
                      'pvr_translator_prepare_locked',
                      'copy_from_user'):
            self.assertNotIn(token, self.body,
                             'observe path must not reach %s' % token)

    def test_returns_zeroed_out(self):
        self.assertIn('pvr_out(cmd, &out, sizeof(out))', self.body)


if __name__ == '__main__':
    unittest.main()
