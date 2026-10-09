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

SOURCE = Path(__file__).resolve().parents[2] / 'kernel/recovery/mt_pvr_bridge.c'


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

    def test_single_flight_running_flag(self):
        # r290: one fire at a time via fire_running (the work clears it);
        # the handler never waits and never submits (r147/r283).
        self.assertIn('translator.fire_running', self.body)
        self.assertIn('-EBUSY', self.body)
        self.assertIn('schedule_work(&translator.fire_work', self.body)

    def test_schedules_without_waiting_or_submitting(self):
        for token in ('submit_tqx_work', 'dma_fence_wait', 'msleep',
                      'wait_event'):
            self.assertNotIn(token, self.body,
                             'handler must only locate and schedule, got %s'
                             % token)

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

    def test_teardown_aborts_then_cancels_first(self):
        teardown = fn_body(self.src, 'pvr_translator_teardown_locked')
        abort_at = teardown.find('fire_abort')
        cancel_at = teardown.find('cancel_work_sync(')
        destroy_at = teardown.find('mt_execution_context_destroy(')
        self.assertTrue(0 <= abort_at < cancel_at < destroy_at,
                        'teardown must abort then cancel the work first')

    def test_scratch_presized(self):
        # r290: the handler sizes strips against the scratch; the work
        # targets the scratch base.
        self.assertIn('MT_TQX_SCRATCH_BYTES', self.body)
        self.assertIn('MT_TQX_SCRATCH_VA', self.work)

    def test_space_fits_table_budget(self):
        # r268: the scratch needs 2048 pages over the original 64;
        # vm_vram_create refuses anything over MT_BOOT_MAX_TABLE_PAGES.
        table = Path(__file__).resolve().parents[2] / 'kernel' / 'mt_mmu_bootstrap.h'
        text = table.read_text()
        m = re.search(r'#define\s+MT_BOOT_MAX_TABLE_PAGES\s+(\d+)U', text)
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1)), 64,
                                'table budget must cover the 2112-page scene')

    def test_chunk_math_recorded_for_work(self):
        # r280/r290: 1280x1024x4 = 5MB never fits the 256KB scratch; the
        # handler records row strips capped at MT_TQX_FIRE_MAX_CHUNKS
        # for the serialized work.
        self.assertIn('MT_TQX_FIRE_MAX_CHUNKS', self.src)
        self.assertIn('fire_nchunks', self.body)
        self.assertIn('fire_chunk_h', self.body)
        self.assertIn('fire_height', self.body)
        self.assertIn('scheduled chunks=%u', self.body)
        self.assertIn('-E2BIG', self.body)

    def test_work_serializes_chunks(self):
        # r290: the work prepares, submits, waits and verifies one
        # chunk at a time (pipelining self-blocks with -EBUSY, r283).
        self.assertIn('submit_tqx_work', self.work)
        self.assertIn('dma_fence_wait_timeout', self.work)
        self.assertIn('chunk %u prepare', self.work)
        self.assertIn('chunk %u submit', self.work)
        self.assertIn('chunk %u fence', self.work)
        self.assertIn('fired=%d chunks=%u verified=%d', self.work)
        self.assertIn('fire_abort', self.work)
        self.assertIn('fire_running', self.work)

    def test_teardown_holds_no_fences(self):
        # r290: each fence is put right after its wait; teardown only
        # aborts + cancels + clears flags, no fence array remains.
        teardown = fn_body(self.src, 'pvr_translator_teardown_locked')
        self.assertNotIn('fire_fences', teardown)
        self.assertNotIn('fire_nfences', teardown)
        self.assertNotIn('translator.fire_fence)', teardown,
                         'singular fence field must be gone')

    def test_to_dst_param_defaults_off(self):
        self.assertRegex(self.src,
                         r'static bool translate_fire_to_dst;')
        self.assertRegex(self.src,
                         r'module_param\(translate_fire_to_dst, bool, 0400\)')

    def test_fire_color_override(self):
        # r308: nonzero translate_fire_color replaces the pool-parsed
        # fill colour (colour sweep without rebuilds).
        self.assertRegex(self.src,
                         r'static unsigned int translate_fire_color;')
        self.assertRegex(self.src,
                         r'module_param\(translate_fire_color, uint, 0400\)')
        self.assertIn('translate_fire_color ? translate_fire_color',
                      self.body)

    def test_work_lands_verified_chunks_in_dst(self):
        # r300: bulk-read once, verify from the buffer, then copy the
        # verified chunk into the UMD pool; bounds-checked, loud.
        # r315: destination pixels start at pool base (UMD compares
        # dest+0 vs source+HEAD over the full surface).
        self.assertIn('fire_dst_host', self.work)
        self.assertIn('u64 dst_off = (u64)c * chunk_rows', self.work)
        self.assertNotIn('MT_TRANSFER_POOL_HEAD', self.work)
        self.assertIn('-ERANGE', self.work)
        self.assertIn('todst=%d', self.work)

    def test_work_reports_result(self):
        self.assertIn('fire_result', self.work)

    def test_observe_waits_for_fire_before_bump(self):
        # r300: the UMD proceeds on bump, so the bump must wait for a
        # scheduled fire (bounded, interruptible); a failed fire fails
        # the submit loudly instead of bumping.
        observe = fn_body(self.src, 'pvr_cmd_tdm_submit3_observe')
        self.assertIn('fire_running', observe)
        self.assertIn('msleep_interruptible', observe)
        self.assertIn('fire_result', observe)
        self.assertNotIn('dma_fence_wait', observe,
                         'no fence waits under handler locks')


if __name__ == '__main__':
    unittest.main()
