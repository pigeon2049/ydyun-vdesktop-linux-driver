#!/usr/bin/env python3
"""Gate the check-only kick translator (r113/r147).

Behind translate_kick (default off = legacy accept-and-inspect). A 0x88:0x4
kick with update_count != 0 is honestly refused; a check-only kick resolves
each UFO (PMR directly, SYNC object via its backing-PMR link) and waits in
PMR host memory before submitting a real empty DM2 marker.
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
    assert m, name + ' not found'
    return m.group(0)


class TranslatorGate(unittest.TestCase):
    def test_param_defaults_off(self):
        src = code()
        self.assertRegex(src, r'static bool translate_kick;')
        self.assertRegex(src, r'module_param\(translate_kick, bool, 0400\)')

    def test_update_refused_check_routed(self):
        src = code()
        body = fn_body(src, 'pvr_cmd_kicksync_submit')
        self.assertRegex(body, r'if \(translate_kick\)')
        self.assertRegex(
            body,
            r'if \(in3\.client_check_count \|\| in3\.client_update_count\)\s*'
            r'return pvr_translate_kick\(file, cmd, &in3\)',
            'any nonzero-count kick must translate; zero-count stays inspect')

    def test_updates_applied_after_fence(self):
        src = code()
        body = fn_body(src, 'pvr_translate_kick')
        self.assertRegex(body, r'uconds\[i\]\.expected = uvals\[i\]')
        self.assertRegex(body,
                         r'memcpy\(\(u8 \*\)uconds\[i\]\.host \+ uconds\[i\]\.offset,')
        # updates publish only after real completion, never before the wait
        self.assertLess(body.index('pvr_translator_wait_fence'),
                        body.index('uconds[i].host'))

    def test_check_expected_values_recorded(self):
        # r148: the check loop once forgot conds[i].expected = vals[i], so
        # every wait compared against calloc-zero and only val==0 ever passed.
        src = code()
        body = fn_body(src, 'pvr_translate_kick')
        self.assertRegex(body, r'conds\[i\]\.expected = vals\[i\]')

    def test_sync_block_links_pmr(self):
        src = code()
        body = fn_body(src, 'pvr_cmd_sync_block')
        self.assertRegex(body, r'obj->arg0 = pmr->handle',
                         'SYNC object must record its PMR for UFO follow')

    def test_resolve_follows_sync_objects(self):
        src = code()
        body = fn_body(src, 'pvr_translator_resolve')
        self.assertIn('MT_PVR_KIND_SYNC', body)
        self.assertRegex(body, r'pvr_pmr_find\(file, obj->arg0\)')


if __name__ == '__main__':
    unittest.main()
