#!/usr/bin/env python3
"""One object lookup helper for handle+kind searches (r189)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'

# Handlers that must resolve through pvr_object_find, not an inline loop.
CONVERTED = [
    'pvr_cmd_heap_destroy',
    'pvr_cmd_ctx_destroy',
    'pvr_cmd_zs_destroy',
    'pvr_cmd_compute_destroy',
    'pvr_cmd_kicksync_destroy',
    'pvr_cmd_tdm_context2_destroy',
    'pvr_cmd_handle_release',
    'pvr_cmd_kicksync_submit',
]


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def function_body(name, text):
    start = re.search(r'^(?:static\s+)?\w[\w\s\*]*\b' + re.escape(name) +
                      r'\s*\([^;]*?\)\s*\{', text, re.M)
    if not start:
        raise AssertionError(f'{name} not found')
    depth = 0
    i = text.index('{', start.start())
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise AssertionError(f'{name} is not brace-balanced')


class ObjectFind(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(BRIDGE.read_text())

    def test_helper_matches_handle_and_kind(self):
        body = function_body('pvr_object_find', self.text)
        self.assertIn('obj->handle == handle && obj->kind == kind', body)
        self.assertIn('return NULL;', body)

    def test_handlers_use_the_helper(self):
        for func in CONVERTED:
            body = function_body(func, self.text)
            self.assertIn('pvr_object_find(', body,
                          '%s must resolve through pvr_object_find' % func)
            self.assertNotIn('list_for_each_entry(obj', body,
                             '%s still carries an inline search loop' % func)

    def test_map_looks_up_the_reservation_once(self):
        body = function_body('pvr_cmd_pmr_map', self.text)
        self.assertEqual(
            body.count('pvr_reservation_find('), 1,
            'map must reuse its validated reservation pointer')


if __name__ == '__main__':
    unittest.main()
