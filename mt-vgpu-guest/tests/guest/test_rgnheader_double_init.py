#!/usr/bin/env python3
"""r455: RgnHeader init must write UMD's double-loop quantity (128 dwords).

r454 [MEASURED] (InitRegionHeaderBuffer inline, decompiled.c): UMD writes
the init pattern TWICE -- two consecutive loops, each writing `local_700`
dwords of integer 1 to the same advancing pointer (total 2 x local_700
dwords, contiguous). local_700 ~= 64 dwords for 64x64, so total init =
512 bytes = 2x the logical RgnHeader size.

r431-r454 wrote only 64 dwords (MT_TA_RGNHEADER_BYTES = 0x100U). r453's
"unfilled real data" hypothesis was falsified by r454: 1s ARE the complete
init; the defect was quantity (half).

The fix (r455): MT_TA_RGNHEADER_INIT_BYTES = 2 * MT_TA_RGNHEADER_BYTES,
used for the stack buffer, the fill loop bound, the BO write size, and the
PAGE_ALIGN alloc in kernel/recovery/mt_pvr_bridge.c. The logical size
(MT_TA_RGNHEADER_BYTES, mt_ta_rgnheader_size()) is unchanged.

Reverse validation: reverting the r455 kernel changes must fail these
tests (old code references only MT_TA_RGNHEADER_BYTES in the init path).
"""
import re
import unittest
from pathlib import Path

REPO = Path("/opt/ydyun-vdesktop-linux-driver")
HDR = REPO / "mt-vgpu-guest/kernel/mt_ta_real.h"
BRIDGE = REPO / "mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c"


class TestRgnHeaderDoubleInitDefine(unittest.TestCase):
    def setUp(self):
        self.hdr = HDR.read_text()

    def test_init_bytes_defined_as_double(self):
        """MT_TA_RGNHEADER_INIT_BYTES = 2 * MT_TA_RGNHEADER_BYTES."""
        self.assertRegex(
            self.hdr,
            r"#define MT_TA_RGNHEADER_INIT_BYTES\s+\(2U \* MT_TA_RGNHEADER_BYTES\)",
            "init bytes must be defined as twice the logical RgnHeader size",
        )

    def test_init_bytes_comment_cites_r454(self):
        """The define documents the r454 double-loop measurement."""
        self.assertRegex(
            self.hdr,
            r"(?s)r454.{0,800}#define MT_TA_RGNHEADER_INIT_BYTES",
            "init bytes define must cite the r454 measurement",
        )

    def test_logical_bytes_unchanged(self):
        """MT_TA_RGNHEADER_BYTES stays 0x100U (logical size)."""
        self.assertRegex(
            self.hdr,
            r"#define MT_TA_RGNHEADER_BYTES 0x100U",
            "logical RgnHeader size must remain 0x100U",
        )


class TestRgnHeaderDoubleInitBridge(unittest.TestCase):
    def setUp(self):
        self.src = BRIDGE.read_text()

    def test_alloc_uses_init_bytes(self):
        """BO alloc covers the full double init quantity."""
        self.assertRegex(
            self.src,
            r"rgn_alloc = PAGE_ALIGN\(MT_TA_RGNHEADER_INIT_BYTES\)",
            "rgn_alloc must use MT_TA_RGNHEADER_INIT_BYTES",
        )

    def test_stack_buffer_uses_init_bytes(self):
        """Stack init buffer is INIT_BYTES sized."""
        self.assertRegex(
            self.src,
            r"u8 rgn_init\[MT_TA_RGNHEADER_INIT_BYTES\]",
            "rgn_init stack buffer must be MT_TA_RGNHEADER_INIT_BYTES",
        )

    def test_fill_loop_uses_init_bytes(self):
        """Fill loop writes 128 dwords (not 64)."""
        self.assertRegex(
            self.src,
            r"for \(i = 0; i < MT_TA_RGNHEADER_INIT_BYTES / sizeof\(u32\); i\+\+\)",
            "fill loop must iterate MT_TA_RGNHEADER_INIT_BYTES dwords",
        )

    def test_bo_write_uses_init_bytes(self):
        """BO write transfers the full double init quantity."""
        self.assertRegex(
            self.src,
            r"pvr_translator_bo_write\(d, &ctx->rgnheader_bo, 0,\s*\n?\s*rgn_init, MT_TA_RGNHEADER_INIT_BYTES\)",
            "BO write size must be MT_TA_RGNHEADER_INIT_BYTES",
        )

    def test_no_single_bytes_init_remaining(self):
        """Old half-quantity init (bare MT_TA_RGNHEADER_BYTES) is gone."""
        for pat, what in [
            (r"rgn_init\[MT_TA_RGNHEADER_BYTES\]", "stack buffer"),
            (r"PAGE_ALIGN\(MT_TA_RGNHEADER_BYTES\)", "alloc size"),
            (
                r"rgn_init, MT_TA_RGNHEADER_BYTES\)",
                "BO write size",
            ),
        ]:
            self.assertNotRegex(
                self.src,
                pat,
                "old half-quantity init must not remain: %s" % what,
            )

    def test_100b_typo_fixed(self):
        """The '0x100B' typo in the init comment is fixed (r454)."""
        self.assertNotRegex(
            self.src, r"0x100B", "'0x100B' typo must be fixed"
        )


if __name__ == "__main__":
    unittest.main()
