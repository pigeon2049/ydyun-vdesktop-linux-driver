#!/usr/bin/env python3
"""Gate the per-context render context layout (r388, R6 Route A).

`kernel/mt_render_context.h` defines struct mt_pvr_render_context (the
per-context real state for R6). This test compiles the header in userspace
and pins sizes/offsets/constants, so the R6 implementation cannot drift
from the r387/r388 design without failing the gate.

Also verifies struct mt_pvr_object carries the render_ctx pointer (via
source inspection, since the struct lives in the .c file).
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "kernel" / "mt_render_context.h"
BRIDGE_C = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"

TU = r"""
#include <stdio.h>
#include "kernel/mt_render_context.h"
int main(void)
{
    printf("sizeof_ctx=%zu\n", sizeof(struct mt_pvr_render_context));
    printf("off_bos=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, bos));
    printf("off_vas=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, vas));
    printf("off_bos_ready=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, bos_ready));
    printf("off_process=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, process));
    printf("off_exec_ctx=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, exec_ctx));
    printf("off_csw=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, csw));
    printf("off_vm=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, vm));
    printf("off_vm_base_va=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, vm_base_va));
    printf("off_resources_ready=%zu\n",
           __builtin_offsetof(struct mt_pvr_render_context, resources_ready));
    printf("stride=%u\n", MT_RENDER_CONTEXT_VA_STRIDE);
    printf("bo_count=%u\n", MT_GFX_CONTEXT_BO_COUNT);
    printf("csw_bytes=%u\n", MT_GFX_CONTEXT_CSW_BYTES);
    return 0;
}
"""

# Design pins from kernel/mt_render_context.h (r388) + r387 §1.2.
# struct mt_bo=88, exec_process=32, exec_ctx=72 (measured via hdr_test).
# Layout: bos[11]=968 @0; vas[11]=88 @968; bos_ready[11]=11 @1056;
#   pad to 1072; process=32 @1072; exec_ctx=72 @1104; exec_ready @1176;
#   csw[248] @1177; pad to 1432; vm @1432; vm_base_va @1440;
#   resources_ready @1448; total 1456 (8-aligned).
EXPECTED = {
    "sizeof_ctx": 1456,
    "off_bos": 0,
    "off_vas": 968,
    "off_bos_ready": 1056,
    "off_process": 1072,
    "off_exec_ctx": 1104,
    "off_csw": 1177,
    "off_vm": 1432,
    "off_vm_base_va": 1440,
    "off_resources_ready": 1448,
    "stride": 16 << 20,
    "bo_count": 11,
    "csw_bytes": 248,
}


def compiled_values():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "rc_layout.c"
        src.write_text(TU)
        binary = Path(tmp) / "rc_layout"
        subprocess.run(
            ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT), str(src), "-o", str(binary)],
            check=True, capture_output=True, text=True,
        )
        out = subprocess.run(
            [str(binary)], check=True, capture_output=True, text=True,
        ).stdout
    vals = {}
    for line in out.strip().split("\n"):
        k, v = line.split("=")
        vals[k] = int(v)
    return vals


class TestRenderContextLayout(unittest.TestCase):
    def test_header_layout_pins(self):
        vals = compiled_values()
        for key, want in EXPECTED.items():
            self.assertEqual(
                vals[key], want,
                f"{key}: got {vals[key]}, want {want} (r388 design drift?)",
            )

    def test_pvr_object_has_render_ctx(self):
        """struct mt_pvr_object must carry the render_ctx pointer (r388)."""
        src = BRIDGE_C.read_text()
        # Find the struct definition and check for the field.
        m = re.search(
            r"struct mt_pvr_object \{(.*?)\};", src, re.DOTALL,
        )
        self.assertIsNotNone(m, "struct mt_pvr_object not found")
        body = m.group(1)
        self.assertIn(
            "struct mt_pvr_render_context *render_ctx;", body,
            "render_ctx pointer missing from struct mt_pvr_object",
        )

    def test_pvr_object_new_zeroes_render_ctx(self):
        """pvr_object_new must use kzalloc (render_ctx defaults to NULL)."""
        src = BRIDGE_C.read_text()
        m = re.search(
            r"static struct mt_pvr_object \*pvr_object_new\(.*?\)\s*\{(.*?)\n\}",
            src, re.DOTALL,
        )
        self.assertIsNotNone(m, "pvr_object_new not found")
        body = m.group(1)
        self.assertIn(
            "kzalloc(sizeof(*obj), GFP_KERNEL)", body,
            "pvr_object_new must kzalloc (render_ctx NULL by default)",
        )


if __name__ == "__main__":
    unittest.main()
