#!/usr/bin/env python3
"""Gate the 0x82:0xC dispatch ctx lifecycle (r369, updated r398).

r368 falsified r367's UAF claim: the TA op takes no context ownership
(it clears work->context and never assigns m->context).

r398 Phase 2 changed the model: ctx is BORROWED from the live render_ctx
(rctx->exec_ctx_ta), never kzalloc'd by the dispatch. Therefore there
must be NO kfree(ctx) anywhere in pvr_cmd_musakickgfx2() -- freeing a
borrowed pointer would corrupt the render context. The old throwaway
kzalloc path (r391 fallback) was removed with the per-file VM.

Reverse validation: re-adding kzalloc/kfree for ctx must turn this red.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
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

    def test_error_path_does_not_free_borrowed_ctx(self):
        # r398: ctx is borrowed from render_ctx; the error path must NOT
        # kfree it (would corrupt the live render context).
        m = re.search(r'if\s*\(ret\)\s*\{([^}]*)\}', self.fn, re.S)
        self.assertIsNotNone(m, 'error path block not found')
        self.assertNotIn('kfree(ctx)', m.group(1),
                         'error path must NOT kfree borrowed ctx (r398)')

    def test_success_path_does_not_free_borrowed_ctx(self):
        # r398: ctx is borrowed; the success path must NOT kfree it.
        m = re.search(
            r'dma_fence_put\(fence\);\s*(.*?)\s*out\.error\s*=\s*0;',
            self.fn, re.S)
        self.assertIsNotNone(m, 'success path region not found')
        self.assertNotIn('kfree(ctx)', m.group(1),
                         'success path must NOT kfree borrowed ctx (r398)')

    def test_no_throwaway_allocation(self):
        # r398: the kzalloc throwaway fallback is gone; ctx always comes
        # from the live render_ctx.
        self.assertNotIn('kzalloc(sizeof(*ctx)', self.fn,
                         'throwaway ctx allocation remains (r398)')

    def test_no_false_ownership_comment(self):
        # The r367 comment claiming ownership transfer to m->context
        # must be gone (falsified by r368).
        self.assertNotIn('ownership transferred to the pending marker',
                         self.fn,
                         'r367 false ownership comment must be removed')


if __name__ == '__main__':
    unittest.main()
