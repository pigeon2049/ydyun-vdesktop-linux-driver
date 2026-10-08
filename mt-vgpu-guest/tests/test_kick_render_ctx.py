#!/usr/bin/env python3
"""Gate the R6-4 kick-side render_ctx parsing (r391, Route A per-context).

Verifies via source inspection that pvr_cmd_musakickgfx2:
1. Resolves h_render_context via pvr_object_find(..., MT_PVR_KIND_CONTEXT).
2. Uses the per-context VM when render_ctx is resources_ready.
3. Falls back to the per-file VM otherwise (Phase 1 marker protection).
4. Logs which VM path was taken (r391 marker).

Reverse validation: removing the pvr_object_find call must fail.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRIDGE_C = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def read_bridge():
    return BRIDGE_C.read_text()


def kick_body(src):
    m = re.search(
        r"static int pvr_cmd_musakickgfx2\(struct mt_pvr_file \*file,\s*"
        r"struct mt_pvr_cmd \*cmd\)\s*\{(.*?)\n\tret = pvr_out\(cmd",
        src, re.S)
    return m.group(1) if m else None


class TestKickRenderCtx(unittest.TestCase):
    def test_resolves_render_context(self):
        body = kick_body(read_bridge())
        self.assertIsNotNone(body, "musakickgfx2 body not found")
        self.assertIn("pvr_object_find(file, in.h_render_context",
                      body, "h_render_context not resolved via pvr_object_find")
        self.assertIn("MT_PVR_KIND_CONTEXT", body,
                      "wrong object kind for render context lookup")

    def test_uses_per_context_vm_when_ready(self):
        body = kick_body(read_bridge())
        self.assertIsNotNone(body, "musakickgfx2 body not found")
        self.assertIn("rctx->resources_ready", body,
                      "resources_ready gate missing")
        self.assertIn("kick_vm = rctx->vm", body,
                      "per-context VM not selected")
        self.assertIn("r391: kick with render_ctx", body,
                      "r391 path log marker missing")

    def test_falls_back_to_per_file(self):
        body = kick_body(read_bridge())
        self.assertIsNotNone(body, "musakickgfx2 body not found")
        self.assertIn("kick_vm = file->ta_vm_ctx", body,
                      "per-file fallback missing")
        # Fallback still creates the per-file VM on demand (r376 V1).
        self.assertIn("mt_bridge_ta_vm_create()", body,
                      "per-file VM creation missing from fallback")

    def test_bind_validation_on_selected_vm(self):
        body = kick_body(read_bridge())
        self.assertIsNotNone(body, "musakickgfx2 body not found")
        self.assertIn("mt_gpu_vm_bind_many(&kick_vm->vm, NULL, 0)", body,
                      "V2 bind validation not run on selected VM")
        self.assertIn("r391 V2: bind empty", body,
                      "r391 V2 log marker missing")

    def test_reverse_validation(self):
        # The test itself documents the reverse check: deleting the
        # pvr_object_find line breaks test_resolves_render_context.
        src = read_bridge()
        self.assertIn("r391 R6-4", src, "r391 marker comment missing")


if __name__ == "__main__":
    unittest.main()
