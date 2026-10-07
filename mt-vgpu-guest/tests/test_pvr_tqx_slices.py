#!/usr/bin/env python3
"""Gate the TQX pool-slices bring-up complement (r261).

When translate_tqx_ctx builds the flavor-1 context, bring-up must also
fill its pool slices with a one-shot copy prepare (non-fatal: DM stays
up on failure; fire checks tqx_slices_ready). Teardown must release
the slices before destroying the context. The upload path needs a BO
read mirror of the write path.
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


class TqxSlicesBringup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        cls.body = fn_body(cls.src, 'pvr_translator_tqx_slices')

    def test_slices_filled_after_context(self):
        prep = fn_body(self.src, 'pvr_translator_prepare_locked')
        self.assertRegex(prep,
                         r'mt_execution_context_create\(&translator\.tqx_context[\s\S]*?'
                         r'pvr_translator_tqx_slices\(d\)',
                         'slices must follow the flavor-1 context create')

    def test_uses_copy_prepare(self):
        self.assertIn('mt_tqx_work_prepare_from_pools(', self.body)
        self.assertIn('mt_tqx_work_cancel(', self.body)

    def test_nonfatal_flag(self):
        self.assertIn('tqx_slices_ready = true', self.body)
        self.assertIn('tqx_slices_ready', fn_body(
            self.src, 'pvr_translator_teardown_locked'))

    def test_teardown_releases_slices(self):
        teardown = fn_body(self.src, 'pvr_translator_teardown_locked')
        self.assertIn('mt_tqx_context_pool_slices_release(', teardown)
        self.assertLess(teardown.index('mt_tqx_context_pool_slices_release('),
                        teardown.index('mt_execution_context_destroy(&translator.tqx_context)'),
                        'slices must release before context destroy')

    def test_upload_has_read_mirror(self):
        read = fn_body(self.src, 'pvr_translator_bo_read')
        self.assertIn('mt_bo_cpu_begin(', read)
        self.assertIn('memcpy_fromio(', read)

    def test_no_execution_path(self):
        for token in ('submit_tqx_work', 'submit_context', 'dma_fence',
                      'dma_fence_wait'):
            self.assertNotIn(token, self.body,
                             'slices prepare must not submit or wait')

    def test_scratch_bound_before_seal(self):
        # r263: binding after the seal is refused; the scratch stream
        # Bos join the pre-seal TQX bind block, not the slices helper.
        src = code()
        self.assertIn('translator.tqx_tmp_src', src)
        self.assertIn('translator.tqx_tmp_dst', src)
        self.assertNotIn('mt_bo_create(&tmp_src', self.body)
        teardown = fn_body(src, 'pvr_translator_teardown_locked')
        self.assertIn('mt_bo_put(&translator.tqx_tmp_dst)', teardown)
        self.assertIn('mt_bo_put(&translator.tqx_tmp_src)', teardown)

    def test_slices_outside_trial_lock(self):
        # r265 (r263 deadlock): the slices prepare takes
        # buffers->lock, which must never nest inside trial_lock.
        # Bring-up drops trial_lock across the slices call;
        # translator_lock (held by all prepare callers) serializes.
        src = code()
        prep = fn_body(src, 'pvr_translator_prepare_locked')
        unlock_at = prep.find('mutex_unlock(&g->trial_lock);')
        slices_at = prep.find('pvr_translator_tqx_slices(d);')
        relock_at = prep.find('mutex_lock(&g->trial_lock);',
                              slices_at)
        self.assertTrue(0 < unlock_at < slices_at < relock_at,
                        'trial_lock must drop across the slices call')

    def test_fail_line_reported(self):
        # r275: every prepare failure reports its source line; silent
        # goto-out chains hid the -22 origin (r274).
        src = code()
        prep = fn_body(src, 'pvr_translator_prepare_locked')
        self.assertIn('fail_at = __LINE__', prep)
        self.assertRegex(prep, r'failed at line %d')
        self.assertIn('fail_at, ret', prep)


if __name__ == '__main__':
    unittest.main()
