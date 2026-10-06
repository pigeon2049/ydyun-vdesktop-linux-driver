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
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
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
        # r182 added a bring-up hook (prepare only); submission stays out.
        for token in ('dma_submit', 'mt_fw_event',
                      'mt_system_submit', 'doorbell', 'submit_tqx_work',
                      'submit_context', 'dma_fence'):
            self.assertNotIn(token, self.body,
                             'observe path must not reach %s' % token)
        self.assertIn('pvr_translator_prepare_locked', self.body)

    def test_dry_run_gated_default_off(self):
        m = re.search(r'static bool translate_transfer;', self.src)
        self.assertIsNotNone(m, 'translate_transfer switch must exist')
        self.assertRegex(self.src,
                         r'module_param\(translate_transfer, bool, 0400\)')

    def test_observe_calls_dry_run_only_when_on(self):
        self.assertRegex(self.body,
                         r'if \(translate_transfer\)[\s\S]*?'
                         r'pvr_submit3_transfer_dry_run')

    def test_dry_run_builds_but_never_submits(self):
        body = fn_body(self.src, 'pvr_submit3_transfer_dry_run')
        for token in ('mt_transfer_pool_parse', 'mt_transfer_fill_rect',
                      'mt_tqx_fill_build'):
            self.assertIn(token, body)
        self.assertIn('-EOPNOTSUPP', body)
        for token in ('submit_tqx_work', 'submit_context', 'dma_fence',
                      'mt_bo_create'):
            self.assertNotIn(token, body,
                             'dry-run must not reach %s' % token)

    def test_fill_destination_zero_initialized(self):
        # r181: stack-uninitialized destination block made builds depend on
        # frame garbage (live only survived on fresh zero stacks).
        fill_h = Path(__file__).resolve().parents[1] / 'kernel/mt_tqx_fill.h'
        src = re.sub(r'/\*.*?\*/', '', fill_h.read_text(), flags=re.S)
        m = re.search(r'struct mt_tqx_destination_input dest(.*?);',
                      src, re.S)
        self.assertIsNotNone(m)
        self.assertIn('= {0}', m.group(0))

    def test_tqx_ctx_gated_default_off(self):
        m = re.search(r'static bool translate_tqx_ctx;', self.src)
        self.assertIsNotNone(m, 'translate_tqx_ctx switch must exist')
        self.assertRegex(self.src,
                         r'module_param\(translate_tqx_ctx, bool, 0400\)')

    def test_tqx_bringup_inside_prepare_before_seal(self):
        # The sealed space refuses binds and re-upload, so the flavor-1
        # context plus command/DMA/state Bos must be built in prepare.
        body = fn_body(self.src, 'pvr_translator_prepare_locked')
        self.assertIn('mt_execution_context_create(&translator.tqx_context',
                      body)
        for tok in ('translator.tqx_cmd', 'translator.tqx_dma',
                    'translator.tqx_state'):
            self.assertIn(tok, body)
        self.assertIn('translator.tqx_ready = true;', body)
        seal_at = body.find('ops->seal(translator.space)')
        ready_at = body.find('translator.tqx_ready = true;')
        self.assertTrue(0 < seal_at and ready_at < seal_at,
                        'TQX objects must be bound before the seal')

    def test_tqx_teardown_wired(self):
        body = fn_body(self.src, 'pvr_translator_teardown_locked')
        for tok in ('mt_execution_context_destroy(&translator.tqx_context)',
                    'mt_bo_put(&translator.tqx_cmd)',
                    'mt_bo_put(&translator.tqx_dma)',
                    'mt_bo_put(&translator.tqx_state)'):
            self.assertIn(tok, body)

    def test_tqx_no_submit_in_bringup(self):
        body = fn_body(self.src, 'pvr_translator_prepare_locked')
        for tok in ('submit_tqx_work', 'submit_context', 'dma_fence'):
            self.assertNotIn(tok, body,
                             'bring-up must not submit (r182)')


if __name__ == '__main__':
    unittest.main()
