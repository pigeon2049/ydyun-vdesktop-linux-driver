#!/usr/bin/env python3
"""Gate the standalone TQX fill-fire module (r282).

mt_live_tqx_fire runs the r280 chunked fire on its OWN render node so the
live session desktop holding renderD128 (r281) no longer blocks validation:
session acquired via pci_get_drvdata (never __symbol_get, never the bridge
translator), own 64-page space + TQX context + slices, own DRM registration,
root-only single-shot run param, bounded fence waits outside trial_lock,
full teardown on rmmod.
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'kernel/recovery/mt_live_tqx_fire.c'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def fn_body(src, name):
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)


class LiveTqxFireModule(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        cls.run_body = fn_body(cls.src, 'run_set')
        cls.prep = fn_body(cls.src, 'prepare')

    def test_own_render_node(self):
        self.assertIn('drm_dev_alloc', self.src)
        self.assertIn('drm_dev_register', self.src)
        self.assertIn('mtlivefire', self.src)
        self.assertNotIn('renderD128', self.src,
                         'must never name the default bridge node')

    def test_session_acquire_without_bridge(self):
        self.assertIn('pci_get_drvdata', self.src)
        self.assertIn('try_module_get', self.src)
        self.assertNotIn('__symbol_get', self.src)
        self.assertNotIn('translator.', self.src,
                         'must not touch bridge translator state')

    def test_space_matches_shrunk_budget(self):
        self.assertIn('MT_TRANSLATE_SPACE_PAGES', self.prep)

    def test_slices_outside_trial_lock(self):
        unlock_at = self.prep.find('mutex_unlock(&d->state.trial_lock);')
        slices_at = self.prep.find('fire_slices();')
        relock_at = self.prep.find('mutex_lock(&d->state.trial_lock);',
                                   slices_at)
        self.assertTrue(0 < unlock_at < slices_at < relock_at,
                        'trial_lock must drop across the slices call (r265)')

    def test_chunk_loop_with_cap(self):
        self.assertIn('MT_FIRE_MAX_CHUNKS', self.src)
        self.assertIn('fences[c]', self.run_body)
        self.assertIn('chunks=%u', self.run_body)
        self.assertIn('-E2BIG', self.run_body)

    def test_single_shot(self):
        self.assertIn('attempted', self.run_body)
        self.assertIn('-EBUSY', self.run_body)

    def test_waits_outside_trial_lock(self):
        # The wait loop runs after the submit path unlocks; the later
        # verify block re-locks legitimately (waits already done).
        self.assertIn('dma_fence_wait_timeout', self.run_body)
        wait_at = self.run_body.find('for (c = 0; c < nchunks; c++) {',
                                      self.run_body.find('goto put;'))
        verify_at = self.run_body.find('if (!ret && pixels)')
        wait_region = self.run_body[wait_at:verify_at]
        self.assertIn('dma_fence_wait_timeout', wait_region)
        self.assertNotIn('mutex_lock', wait_region,
                         'fence waits must stay outside trial_lock')

    def test_teardown_symmetry(self):
        teardown = fn_body(self.src, 'release_all')
        for token in ('mt_tqx_context_pool_slices_release',
                      'mt_execution_context_destroy',
                      'mt_execution_process_destroy',
                      'address_spaces.ops->destroy', 'mt_bo_put'):
            self.assertIn(token, teardown)
        self.assertIn('drm_dev_unregister', self.src)

    def test_fail_line_reported(self):
        self.assertIn('fail_at = __LINE__', self.prep)
        self.assertIn('failed at line %d', self.src)


if __name__ == '__main__':
    unittest.main()
