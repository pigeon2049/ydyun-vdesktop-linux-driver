#!/usr/bin/env python3
"""Gate the submit3 update writeback (r297).

translate_submit3_bump (default off) applies the UMD's own update values
into their sync PMRs at observe time, mirroring the live-proven
translator kick writeback: cap + kcalloc + copy_from_user, resolve ALL
via pvr_translator_resolve before writing ANY (no half-written sets),
loud failures, file->lock only.
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
    m = re.search(r'static int %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)


class Submit3Bump(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        cls.body = fn_body(cls.src, 'pvr_submit3_bump_updates')
        cls.observe = fn_body(cls.src, 'pvr_cmd_tdm_submit3_observe')

    def test_param_defaults_off(self):
        self.assertRegex(self.src,
                         r'static bool translate_submit3_bump;')
        self.assertRegex(self.src,
                         r'module_param\(translate_submit3_bump, bool, 0400\)')

    def test_count_cap_loud(self):
        self.assertIn('MT_PVR_SUBMIT3_UPDATE_MAX', self.body)
        self.assertIn('-E2BIG', self.body)

    def test_copies_user_arrays(self):
        for token in ('update_handles', 'update_offsets', 'update_values',
                      'copy_from_user'):
            self.assertIn(token, self.body)
        self.assertIn('-EFAULT', self.body)

    def test_resolve_before_write(self):
        resolve_at = self.body.find('pvr_translator_resolve')
        write_at = self.body.find('memcpy')
        self.assertTrue(0 < resolve_at < write_at,
                        'resolve ALL before writing ANY (no half sets)')

    def test_uses_umd_values_verbatim(self):
        self.assertIn('values[i]', self.body)

    def test_frees_everything(self):
        self.assertEqual(self.body.count('kcalloc'), 4)
        self.assertGreaterEqual(self.body.count('kfree'), 4)

    def test_gated_in_observe(self):
        self.assertIn('translate_submit3_bump', self.observe)
        self.assertIn('pvr_submit3_bump_updates', self.observe)
        self.assertIn('pvr_out(cmd, &out, sizeof(out))', self.observe)


if __name__ == '__main__':
    unittest.main()
