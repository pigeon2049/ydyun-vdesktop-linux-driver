#!/usr/bin/env python3
"""Gate the SubmitTransfer3 accept-and-log handler (r174).

0x89:0xa must reach pvr_cmd_tdm_submit3_observe, which reports the named
CCB window and returns 0 without executing anything: bounded scan, context
handle validated, nested check/update/PMR-sync arrays untouched, no
firmware/DMA/translator path reachable.
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
    m = re.search(r'static int %s\(.*?^}' % re.escape(name), src, re.S | re.M)
    assert m, '%s not found' % name
    return m.group(0)


class TdmSubmit3Observe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        m = re.search(r'case MT_PVR_BRIDGE_RGXTDM:(.*?)default:',
                      cls.src, re.S)
        assert m, 'RGXTDM dispatch block not found'
        cls.dispatch = m.group(1)
        cls.body = fn_body(cls.src, 'pvr_cmd_tdm_submit3_observe')

    def test_submit3_routed(self):
        self.assertRegex(self.dispatch,
                         r'case 0xa:[\s\S]*?pvr_cmd_tdm_submit3_observe',
                         '0x89:0xa must reach the observe handler, not -ENOTTY')

    def test_window_bounded(self):
        self.assertIn('MT_PVR_SUBMIT3_LOG_MAX', self.body)
        self.assertIn('ccb_bytes > MT_PVR_SUBMIT3_LOG_MAX', self.body)

    def test_context_validated(self):
        self.assertIn('MT_PVR_KIND_TDM_CONTEXT', self.body)
        self.assertIn('-ENOENT', self.body)

    def test_nested_arrays_untouched(self):
        for field in ('check_devvar_offset', 'check_value', 'check_ufo_block',
                      'update_devvar_offset', 'update_value', 'update_ufo_block',
                      'pmr_sync_handles', 'pmr_sync_access_flags'):
            self.assertNotIn(field, self.body,
                             'observe path must not read nested %s' % field)

    def test_no_execution_path(self):
        for token in ('dma_submit', 'mt_fw_event', 'translator',
                      'mt_system_submit', 'doorbell'):
            self.assertNotIn(token, self.body,
                             'observe path must not reach %s' % token)


if __name__ == '__main__':
    unittest.main()
