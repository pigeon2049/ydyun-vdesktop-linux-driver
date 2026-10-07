#!/usr/bin/env python3
"""Gate the real SyncPrimSet handler (r220).

0x2:0x2 must reach pvr_cmd_syncprim_set (not pvr_stub_ok): IN16
{sync handle, dword index, value} per the hash-verified 5.2.0 UMD
wrapper, resolved exactly like the translator wait path (raw PMR
handle, or a SYNC-kind object following its backing PMR link),
range-checked, written to PMR host memory, zeroed 4B OUT. No fence,
no submission, no wakeup (waiters poll).
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'kernel/recovery/mt_pvr_bridge.c'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def fn_body(src, name):
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)


class SyncPrimSetWrite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        m = re.search(r'case MT_PVR_BRIDGE_SYNC:(.*?)case MT_PVR_BRIDGE_\w+:',
                      cls.src, re.S)
        assert m, 'SYNC dispatch block not found'
        cls.dispatch = m.group(1)
        cls.body = fn_body(cls.src, 'pvr_cmd_syncprim_set')

    def test_syncprimset_routed(self):
        self.assertRegex(self.dispatch,
                         r'case MT_PVR_FN_SYNCPRIMSET:[\s\S]*?pvr_cmd_syncprim_set',
                         '0x2:0x2 must reach the write handler, not stub_ok')
        m = re.search(r'case MT_PVR_FN_SYNCPRIMSET:(.*?)case \w',
                      self.dispatch, re.S)
        self.assertIsNotNone(m)
        self.assertNotIn('pvr_stub_ok', m.group(1),
                         'stub_ok must no longer serve 0x2:0x2')

    def test_wire_layout_matches_wrapper(self):
        self.assertIn('mt_pvr_syncprimset_in', self.body)
        self.assertIn('mt_pvr_syncprimset_out', self.body)

    def test_resolves_like_translator_wait(self):
        self.assertIn('pvr_translator_resolve(', self.body)

    def test_index_scaled_and_bounded(self):
        self.assertIn('sizeof(u32)', self.body)
        self.assertIn('-ERANGE', self.body)

    def test_writes_host_memory_only(self):
        self.assertIn('memcpy(', self.body)
        self.assertIn('cond.host', self.body)

    def test_no_execution_or_wakeup(self):
        for token in ('dma_submit', 'mt_fw_event', 'mt_system_submit',
                      'doorbell', 'submit_tqx_work', 'submit_context',
                      'dma_fence', 'wake_up', 'complete('):
            self.assertNotIn(token, self.body,
                             'set path must not reach %s' % token)

    def test_answers_zeroed_out(self):
        self.assertIn('pvr_out(cmd, &out, sizeof(out))', self.body)


if __name__ == '__main__':
    unittest.main()
