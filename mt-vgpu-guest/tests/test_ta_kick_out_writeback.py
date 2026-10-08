#!/usr/bin/env python3
"""Gate the 0x82:0xC OUT.writeback error reporting (r373).

Verifies that pvr_cmd_musakickgfx2() checks pvr_out()'s return value:
- on failure: pr_warn with "OUT writeback failed", returns the error
  (no misleading "submitted wire" info log)
- on success: pr_info "submitted wire=%u", returns 0

Reverse validation: reverting the r373 fix (bare `return pvr_out(...)`)
must fail this test.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "kernel" / "recovery" / "mt_pvr_bridge.c"


def get_musakickgfx2_body():
    text = SRC.read_text()
    m = re.search(
        r"static int pvr_cmd_musakickgfx2\(.*?^}",
        text, re.M | re.S)
    assert m, "pvr_cmd_musakickgfx2 not found"
    return m.group(0)


class TestOutWriteback(unittest.TestCase):
    def test_pvr_out_return_checked(self):
        body = get_musakickgfx2_body()
        # Must capture pvr_out's return value, not return it blindly
        self.assertRegex(
            body,
            r"ret\s*=\s*pvr_out\(cmd,\s*&out,\s*sizeof\(out\)\)",
            "pvr_out return value must be captured in ret")

    def test_writeback_failure_warns(self):
        body = get_musakickgfx2_body()
        # On failure: pr_warn mentioning OUT writeback failed + wire id
        self.assertIn("OUT writeback failed", body,
                      "writeback failure must be logged")
        self.assertRegex(
            body,
            r"pr_warn\(.*OUT writeback failed.*wire=%u",
            "warning must include wire id")

    def test_no_misleading_submitted_on_failure(self):
        body = get_musakickgfx2_body()
        # The "submitted wire" info log must come AFTER the pvr_out check,
        # i.e. only on success. Find positions.
        warn_pos = body.find("OUT writeback failed")
        info_pos = body.find('submitted wire=%u\\n", wire_id);\n\treturn 0;')
        self.assertGreater(warn_pos, 0)
        self.assertGreater(info_pos, warn_pos,
                           "'submitted wire' info must follow the error check")

    def test_out_struct_layout(self):
        # OUT.update_fence at offset 4 (KMD header: eError, hUpdateFence,
        # hUpdateFence3D -- 4 bytes each, 12 bytes total)
        wire_h = (ROOT / "kernel" / "mt_pvr_wire.h").read_text()
        m = re.search(
            r"struct MT_PVR_PACKED mt_pvr_musakickgfx2_out \{(.*?)\};",
            wire_h, re.S)
        self.assertIsNotNone(m, "OUT struct not found")
        fields = m.group(1)
        # error first, then update_fence, then update_fence_3d
        self.assertLess(fields.find("error"), fields.find("update_fence"))
        self.assertLess(fields.find("update_fence;"),
                        fields.find("update_fence_3d"))


if __name__ == "__main__":
    unittest.main()
