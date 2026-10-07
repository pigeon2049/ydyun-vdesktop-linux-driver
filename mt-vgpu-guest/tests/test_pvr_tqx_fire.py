#!/usr/bin/env python3
"""Gate the live TQX fill fire path (r267).

translate_tqx_fire (default off) submits the dry-run program at the
pre-seal scratch surface and verifies asynchronously: no handler-held
waits (r147), no file-object touches from the work (the UMD file may
be gone), teardown cancels the work first, one fire at a time with
fence reuse.
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


class TqxFirePath(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        cls.body = fn_body(cls.src, 'pvr_submit3_transfer_fire')
        cls.work = fn_body(cls.src, 'pvr_translator_fire_work')

    def test_param_defaults_off(self):
        self.assertRegex(self.src,
                         r'static bool translate_tqx_fire;')
        self.assertRegex(self.src,
                         r'module_param\(translate_tqx_fire, bool, 0400\)')

    def test_fire_gated_in_observe(self):
        self.assertRegex(self.src,
                         r'if \(translate_tqx_fire\)[\s\S]*?'
                         r'pvr_submit3_transfer_fire')

    def test_requires_slices_ready(self):
        self.assertIn('translator.tqx_slices_ready', self.body)
        self.assertIn('-EOPNOTSUPP', self.body)

    def test_single_flight_fence_reuse(self):
        self.assertIn('translator.fire_pending', self.body)
        self.assertIn('-EBUSY', self.body)
        self.assertIn('dma_fence_is_signaled(', self.body)

    def test_submits_without_waiting(self):
        self.assertIn('submit_tqx_work', self.body)
        for token in ('dma_fence_wait', 'msleep', 'wait_event'):
            self.assertNotIn(token, self.body,
                             'fire must not wait under handler locks')

    def test_work_touches_no_files(self):
        for token in ('mt_pvr_file', 'file->', 'pvr_pmr_find',
                      'copy_from_user'):
            self.assertNotIn(token, self.work,
                             'async work must not reach %s' % token)
        self.assertIn('pvr_translator_bo_read(', self.work)

    def test_work_init_before_any_teardown(self):
        # r276: teardown cancels on failed-prepare cleanup too, so the
        # work must be INITed at prepare entry, not only on success.
        src = code()
        prep = fn_body(src, 'pvr_translator_prepare_locked')
        init_at = prep.find('INIT_WORK(&translator.fire_work')
        acquire_at = prep.find('pvr_session_acquire(')
        self.assertTrue(0 < init_at < acquire_at,
                        'work must INIT before anything can fail')

    def test_teardown_cancels_first(self):
        teardown = fn_body(self.src, 'pvr_translator_teardown_locked')
        cancel_at = teardown.find('cancel_work_sync(')
        destroy_at = teardown.find('mt_execution_context_destroy(')
        self.assertTrue(0 <= cancel_at < destroy_at,
                        'teardown must cancel the work first')

    def test_scratch_presized(self):
        self.assertIn('MT_TQX_SCRATCH_VA', self.body)
        self.assertIn('MT_TQX_SCRATCH_BYTES', self.body)

    def test_space_fits_table_budget(self):
        # r268: the scratch needs 2048 pages over the original 64;
        # vm_vram_create refuses anything over MT_BOOT_MAX_TABLE_PAGES.
        table = Path(__file__).resolve().parents[1] / 'kernel' / 'mt_mmu_bootstrap.h'
        text = table.read_text()
        m = re.search(r'#define\s+MT_BOOT_MAX_TABLE_PAGES\s+(\d+)U', text)
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1)), 2112,
                                'table budget must cover the 2112-page scene')


if __name__ == '__main__':
    unittest.main()
