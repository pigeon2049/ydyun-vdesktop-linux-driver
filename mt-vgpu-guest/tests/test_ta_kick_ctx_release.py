#!/usr/bin/env python3
"""Gate the 0x82:0xC dispatch ctx lifecycle (r369).

r368 falsified r367's UAF claim: the TA op takes no context ownership
(it clears work->context and never assigns m->context), so the
dispatch-allocated ctx is unreferenced after the op returns and MUST be
freed on the success path. r367 removed that kfree (leak ~64B/dispatch).

This test statically verifies pvr_cmd_musakickgfx2() frees ctx on BOTH
the error path and the success path. Removing the success-path kfree
(simulating r367's bug) must turn this test red (reverse validation).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRIDGE = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def extract_function(src, name):
    """Extract a static function body by brace matching."""
    m = re.search(r'static int ' + re.escape(name) + r'\([^)]*\)\s*\{', src)
    assert m, 'function %s not found' % name
    start = m.end() - 1
    depth = 0
    for i in range(start, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    raise AssertionError('unbalanced braces in %s' % name)


class TestMusakickgfx2CtxRelease(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = BRIDGE.read_text(encoding='utf-8')
        cls.fn = extract_function(cls.src, 'pvr_cmd_musakickgfx2')

    def test_error_path_frees_ctx(self):
        # if (ret) { ... kfree(ctx); return ret; }
        m = re.search(r'if\s*\(ret\)\s*\{([^}]*)\}', self.fn, re.S)
        self.assertIsNotNone(m, 'error path block not found')
        self.assertIn('kfree(ctx)', m.group(1),
                      'error path must kfree(ctx)')

    def test_success_path_frees_ctx(self):
        # After dma_fence_put(fence) and before out.error = 0,
        # the success path must kfree(ctx) (r368: clean release, no UAF).
        m = re.search(
            r'dma_fence_put\(fence\);\s*(.*?)\s*out\.error\s*=\s*0;',
            self.fn, re.S)
        self.assertIsNotNone(m, 'success path region not found')
        self.assertIn('kfree(ctx)', m.group(1),
                      'success path must kfree(ctx) (r367 removed it -> leak)')

    def test_no_false_ownership_comment(self):
        # The r367 comment claiming ownership transfer to m->context
        # must be gone (falsified by r368).
        self.assertNotIn('ownership transferred to the pending marker',
                         self.fn,
                         'r367 false ownership comment must be removed')


if __name__ == '__main__':
    unittest.main()
