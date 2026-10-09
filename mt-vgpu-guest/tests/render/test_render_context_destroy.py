#!/usr/bin/env python3
"""Gate the R6-3 Destroy realization (r390, Route A per-context).

Verifies via source inspection that:
1. mt_render_context_destroy() exists with reverse-order teardown.
2. Exec context is destroyed before exec process (owners accounting).
3. BOs are put before VM destroy (VM fini drops its own refs).
4. Partial-init guards exist (exec_ready/bos_ready/vm-NULL).
5. pvr_cmd_handle_release() calls destroy for real contexts.
6. Create's out_rollback reuses mt_render_context_destroy (single path).

Reverse validation: removing the destroy call from handle_release must fail.
"""

import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
BRIDGE_C = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def read_bridge():
    return BRIDGE_C.read_text()


class TestRenderContextDestroy(unittest.TestCase):
    def test_destroy_function_exists(self):
        src = read_bridge()
        self.assertIn("static void mt_render_context_destroy(",
                      src, "mt_render_context_destroy not defined")
        self.assertIn(
            "static void mt_render_context_destroy(struct mt_pvr_render_context *ctx);",
            src, "forward declaration not found")

    def test_reverse_order_exec_before_vm(self):
        src = read_bridge()
        m = re.search(r"static void mt_render_context_destroy\(struct mt_pvr_render_context \*ctx\)\s*\{(.*?)\n\}\n",
                      src, re.S)
        self.assertIsNotNone(m, "destroy body not found")
        body = m.group(1)
        p_ctx = body.find("mt_execution_context_destroy")
        p_proc = body.find("mt_execution_process_destroy")
        p_vm = body.find("mt_render_context_vm_destroy")
        self.assertGreater(p_ctx, 0, "exec context destroy not in body")
        self.assertGreater(p_proc, p_ctx,
                           "exec process must be destroyed after exec context")
        self.assertGreater(p_vm, p_proc,
                           "VM destroy must come after exec teardown "
                           "(fini returns -EBUSY while owners>0)")

    def test_bo_put_before_vm(self):
        src = read_bridge()
        m = re.search(r"static void mt_render_context_destroy\(struct mt_pvr_render_context \*ctx\)\s*\{(.*?)\n\}\n",
                      src, re.S)
        body = m.group(1)
        p_put = body.find("mt_bo_put(&ctx->bos[i])")
        p_vm = body.find("mt_render_context_vm_destroy")
        self.assertGreater(p_put, 0, "BO put not in destroy body")
        self.assertGreater(p_vm, p_put,
                           "VM destroy must come after BO puts")

    def test_partial_init_guards(self):
        src = read_bridge()
        m = re.search(r"static void mt_render_context_destroy\(struct mt_pvr_render_context \*ctx\)\s*\{(.*?)\n\}\n",
                      src, re.S)
        body = m.group(1)
        self.assertIn("if (ctx->exec_ready)", body,
                      "exec_ready guard missing (partial init unsafe)")
        self.assertIn("if (ctx->bos_ready[i])", body,
                      "bos_ready guard missing (partial init unsafe)")
        self.assertIn("if (ctx->vm)", body,
                      "vm NULL guard missing (partial init unsafe)")
        self.assertIn("if (!ctx)", body, "NULL ctx guard missing")

    def test_handle_release_hooks_destroy(self):
        src = read_bridge()
        m = re.search(r"static int pvr_cmd_handle_release\(.*?^\}\n",
                      src, re.S | re.M)
        self.assertIsNotNone(m, "pvr_cmd_handle_release not found")
        body = m.group(0)
        self.assertIn("mt_render_context_destroy(obj->render_ctx)",
                      body, "handle_release does not call destroy")
        self.assertIn("kfree(obj->render_ctx)", body,
                      "render_ctx struct not freed in handle_release")
        self.assertIn("MT_PVR_KIND_CONTEXT", body,
                      "kind check missing in handle_release")

    def test_rollback_reuses_destroy(self):
        src = read_bridge()
        # out_rollback must delegate to mt_render_context_destroy (single path)
        m = re.search(r"out_rollback:\n(.*?)\nout_release:", src, re.S)
        self.assertIsNotNone(m, "out_rollback block not found")
        block = m.group(1)
        self.assertIn("mt_render_context_destroy(ctx)", block,
                      "out_rollback must reuse mt_render_context_destroy")
        # The old inline exec-destroy duplication must be gone
        self.assertNotIn("mt_execution_context_destroy(&ctx->exec_ctx);\n"
                         "\t\tmt_execution_process_destroy(&ctx->process);",
                         block, "stale inline rollback still present")

    def test_file_release_hooks_destroy(self):
        # V4: pvr_file_release must tear down render_ctx on file close
        src = read_bridge()
        m = re.search(r"static void pvr_file_release\(.*?^\}\n",
                      src, re.S | re.M)
        self.assertIsNotNone(m, "pvr_file_release not found")
        body = m.group(0)
        self.assertIn("mt_render_context_destroy(obj->render_ctx)",
                      body, "pvr_file_release does not destroy render_ctx")
        self.assertIn("kfree(obj->render_ctx)", body,
                      "render_ctx struct not freed in pvr_file_release")

    def test_state_cleared(self):
        src = read_bridge()
        m = re.search(r"static void mt_render_context_destroy\(struct mt_pvr_render_context \*ctx\)\s*\{(.*?)\n\}\n",
                      src, re.S)
        body = m.group(1)
        self.assertIn("ctx->exec_ready = false;", body,
                      "exec_ready not cleared")
        self.assertIn("ctx->bos_ready[i] = false;", body,
                      "bos_ready not cleared")
        self.assertIn("ctx->vm = NULL;", body, "vm not cleared")
        self.assertIn("ctx->resources_ready = false;", body,
                      "resources_ready not cleared")


if __name__ == "__main__":
    unittest.main()
