#!/usr/bin/env python3
"""Name the 0x88 kicksync function IDs compared in kicksync_submit (r187)."""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
WIRE = ROOT / 'kernel' / 'mt_pvr_wire.h'
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'

PINNED = {
    'MT_PVR_FN_RGXKICKSYNC2': '0x2U',
    'MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY': '0x3U',
    'MT_PVR_FN_RGXKICKSYNC3': '0x4U',
}


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


class KicksyncFnIds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wire = WIRE.read_text()
        cls.submit = function_body(
            'pvr_cmd_kicksync_submit', strip_comments(BRIDGE.read_text()))

    def test_wire_pins_each_function_id(self):
        for name, literal in PINNED.items():
            self.assertRegex(
                self.wire,
                r'#define\s+%s\s+%s\b' % (name, re.escape(literal)),
                '%s must keep its wire value %s' % (name, literal))

    def test_submit_compares_by_name(self):
        for name in PINNED:
            self.assertIn('function == %s' % name, self.submit,
                          'kicksync_submit must compare %s by name' % name)
        self.assertIsNone(
            re.search(r'function\s*==\s*0x[0-9a-fA-F]+', self.submit),
            'no bare function-ID comparison may remain in kicksync_submit')


if __name__ == '__main__':
    unittest.main()
