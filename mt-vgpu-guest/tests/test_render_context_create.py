#!/usr/bin/env python3
"""Gate the R6-2 Create realization (r389, Route A per-context).

Verifies via source inspection that:
1. mt_render_context_create() exists with the 11-BO allocation loop.
2. pvr_cmd_render2_create() calls mt_render_context_create (not empty token).
3. Rollback logic is present (reverse-order cleanup on failure).
4. CSW build and exec process/context creation are wired.
5. MT_RENDER_CONTEXT_VA_BASE is defined.

Reverse validation: removing the mt_render_context_create call must fail.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRIDGE_C = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def read_bridge():
    return BRIDGE_C.read_text()


class TestRenderContextCreate(unittest.TestCase):
    def test_create_function_exists(self):
        src = read_bridge()
        self.assertIn("static int mt_render_context_create(",
                      src, "mt_render_context_create not defined")

    def test_va_base_defined(self):
        src = read_bridge()
        m = re.search(r"#define MT_RENDER_CONTEXT_VA_BASE\s+(0x[0-9a-fA-F]+)", src)
        self.assertIsNotNone(m, "MT_RENDER_CONTEXT_VA_BASE not defined")
        self.assertEqual(m.group(1).lower(), "0x70000000",
                         "VA base should be 0x70000000 for V1")

    def test_11bo_loop(self):
        src = read_bridge()
        # The loop must iterate MT_GFX_CONTEXT_BO_COUNT times
        self.assertIn("for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)",
                      src, "11-BO loop not found")
        # Must call mt_bo_create with d->buffers
        self.assertIn("mt_bo_create(&ctx->bos[i], d->buffers.ops, &d->buffers,",
                      src, "mt_bo_create call not found")
        # Must write initial data
        self.assertIn("pvr_translator_bo_write(d, &ctx->bos[i], 0,",
                      src, "initial data write not found")
        self.assertIn("mt_gfx_bo_init_metas[i].data",
                      src, "init meta data not referenced")
        # Must bind via mt_gpu_vm_bind_many
        self.assertIn("mt_gpu_vm_bind_many(&tvm->vm, &binding, 1)",
                      src, "bind_many call not found")

    def test_per_context_vm_create(self):
        src = read_bridge()
        self.assertIn("mt_render_context_vm_create(d, &ctx->pt_bo)",
                      src, "per-context VM create not called")
        self.assertIn("struct mt_bo pt_bo;",
                      open(str(ROOT / "kernel" / "mt_render_context.h")).read(),
                      "pt_bo not in struct")

    def test_csw_build(self):
        src = read_bridge()
        self.assertIn("mt_gfx_context_build_csw(ctx->csw, sizeof(ctx->csw),",
                      src, "CSW build not found")

    def test_exec_create(self):
        src = read_bridge()
        self.assertIn("mt_execution_process_create(&d->execution, &ctx->process,",
                      src, "exec process create not found")
        self.assertIn("mt_execution_context_create(&ctx->exec_ctx_3d, &ctx->process, 5, 0)",
                      src)
        # r397: TA exec context (node_type=2 -> DM3)
        self.assertIn("mt_execution_context_create(&ctx->exec_ctx_ta, &ctx->process, 2, 0)",
                      src, "exec context create (node_type=5) not found")

    def test_rollback(self):
        src = read_bridge()
        # Rollback label must exist
        self.assertIn("out_rollback:", src, "rollback label not found")
        # r390: rollback delegates to the shared R6-3 destroy path
        # (reverse-order teardown lives in mt_render_context_destroy,
        # gated by test_render_context_destroy.py)
        m = re.search(r"out_rollback:\n(.*?)\nout_release:", src, re.S)
        self.assertIsNotNone(m, "out_rollback block not found")
        self.assertIn("mt_render_context_destroy(ctx)", m.group(1),
                      "out_rollback must delegate to mt_render_context_destroy")

    def test_resources_ready_gated(self):
        src = read_bridge()
        # resources_ready only set on full success (after exec creation)
        # Find the function and verify ordering
        # Find the DEFINITION (not the forward decl): look for '{' after the signature
        decl_pos = 0
        func_start = -1
        while True:
            pos = src.find("static int mt_render_context_create(", decl_pos)
            if pos == -1:
                break
            # Check if followed by '{' (definition) vs ';' (declaration)
            after = src[pos:pos+200]
            if '{' in after.split(';')[0]:
                func_start = pos
                break
            decl_pos = pos + 1
        self.assertGreater(func_start, 0, "definition not found")
        # End at the next static function definition
        func_end = src.find("\nstatic int pvr_cmd_render2_create(", func_start)
        func = src[func_start:func_end]
        ready_pos = func.find("ctx->resources_ready = true;")
        exec_pos = func.find("ctx->exec_ready = true;")
        self.assertGreater(ready_pos, 0, "resources_ready not set")
        self.assertGreater(exec_pos, 0, "exec_ready not set")
        self.assertGreater(ready_pos, exec_pos,
                           "resources_ready must be set after exec_ready")

    def test_render2_calls_create(self):
        src = read_bridge()
        # pvr_cmd_render2_create must call mt_render_context_create
        # (not the old empty-token behavior)
        func_start = src.find("static int pvr_cmd_render2_create(")
        # Find the next function after it (pvr_cmd_handle_release)
        func_end = src.find("static int pvr_cmd_handle_release(", func_start)
        func = src[func_start:func_end]
        self.assertIn("mt_render_context_create(file, obj->render_ctx)",
                      func, "pvr_cmd_render2_create does not call mt_render_context_create")
        # Old behavior (just minting token) must be gone
        self.assertNotIn("(void)in;\n\treturn pvr_out(cmd, &out, sizeof(out));",
                         func.replace(" ", "").replace("\t", ""),
                         "old empty-token behavior still present")


if __name__ == "__main__":
    unittest.main()
