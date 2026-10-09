#!/usr/bin/env python3
"""r423 T5 gate: TA buffer Header integrity.

r422 root-cause: mt_ta_real_buffer_build() wrote 40B Entries at buf+0,
polluting Header fields at 0x10/0x18/0x20 (Entry Q2/Q3/Q4 overlap).
Firmware reads Header via psKickTA (RGXSubmitTA, decompiled.c:54365)
and hung on garbage (r421 timeout, 5s -> -ETIMEDOUT).
r414 proved all-zero Header completes ("no work", 219us).

360B TA buffer is Header (0x00-0x160) + Entries dual-zone (r422).
Entries do NOT belong in this buffer. Header-only (r423): zero the
buffer, set TA_buf+0x10 = target_va (-> psKickTA[1] render target).

Rule enforced here: mt_ta_real_buffer_build() must NEVER place a
struct mt_ta_entry_simple at buf+0 (or any buf offset < 0x68).
The function must be Header-only.

Reverse validation: reintroduce
    (struct mt_ta_entry_simple *)(buf + i * MT_TA_ENTRY_SIMPLE_BYTES)
-> test_no_entry_struct_in_buffer_build must FAIL.
"""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir

KERNEL = get_kernel_dir()


def _get_buffer_build_body():
    """Extract mt_ta_real_buffer_build() body from mt_ta_real.h."""
    text = (KERNEL / "mt_ta_real.h").read_text()
    # Find function start.
    m = re.search(
        r"static inline int mt_ta_real_buffer_build\([^)]*\)\s*\{",
        text,
    )
    assert m, "mt_ta_real_buffer_build not found"
    start = m.end()
    # Brace matching to find function end.
    depth = 1
    i = start
    while depth > 0 and i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[start:i]


class TestHeaderIntegrity(unittest.TestCase):
    def test_no_entry_struct_in_buffer_build(self):
        """T5: buffer_build must not place mt_ta_entry_simple in buf."""
        body = _get_buffer_build_body()
        # Ban: declaring or casting to struct mt_ta_entry_simple.
        self.assertNotIn(
            "struct mt_ta_entry_simple",
            body,
            "T5 FAIL: Entry struct in buffer_build pollutes Header (r422):\n"
            + body,
        )

    def test_no_entry_offset_arithmetic(self):
        """T5: no (buf + i * MT_TA_ENTRY_SIMPLE_BYTES) pattern."""
        body = _get_buffer_build_body()
        # Ban: buffer offset arithmetic for Entries.
        hits = re.findall(r"buf\s*\+\s*i\s*\*", body)
        self.assertEqual(
            hits,
            [],
            "T5 FAIL: Entry offset arithmetic in buffer_build (r422):\n"
            + body,
        )

    def test_header_target_va_set(self):
        """T5: Header+0x10 must be set from target_va (r422 [MEASURED])."""
        body = _get_buffer_build_body()
        # Must write target_va to buf+MT_TA_BUF_HDR_TARGET_VA.
        self.assertRegex(
            body,
            r"\*\(u64 \*\)\(buf \+ MT_TA_BUF_HDR_TARGET_VA\)\s*=\s*target_va",
            "T5 FAIL: Header+0x10 not set from target_va",
        )

    def test_n_entries_must_be_zero(self):
        """T5: n_entries must be 0 (Header-only, r423)."""
        body = _get_buffer_build_body()
        # Must reject non-zero n_entries.
        self.assertRegex(
            body,
            r"if\s*\(n_entries\s*!=\s*0\)",
            "T5 FAIL: n_entries!=0 not rejected (Header-only requires 0)",
        )

    def test_header_constant_defined(self):
        """T5: MT_TA_BUF_HDR_TARGET_VA must be 0x10 (r422 [MEASURED])."""
        text = (KERNEL / "mt_ta_real.h").read_text()
        m = re.search(
            r"#define\s+MT_TA_BUF_HDR_TARGET_VA\s+(0x[0-9a-fA-F]+)U", text
        )
        self.assertIsNotNone(m, "MT_TA_BUF_HDR_TARGET_VA not defined")
        self.assertEqual(int(m.group(1), 16), 0x10)



    def test_tile_pack_constants(self):
        """T5 (r439): tile-pack offsets must be 0x50/0x58 (r438 [MEASURED])."""
        text = (KERNEL / "mt_ta_real.h").read_text()
        for name, want in (
            ("MT_TA_BUF_HDR_TILE_PACK_X", 0x50),
            ("MT_TA_BUF_HDR_TILE_PACK_Y", 0x58),
        ):
            m = re.search(r"#define\s+" + name + r"\s+(0x[0-9a-fA-F]+)U", text)
            self.assertIsNotNone(m, name + " not defined")
            self.assertEqual(int(m.group(1), 16), want)

    def test_header_tile_pack_writes(self):
        """T5 (r439): Header+0x50/+0x58 must carry mt_ta_tile_pack(w/h).

        UMD (FUN_00184220, r438 [MEASURED]) packs tile counts into bits
        48-53 of the Header qwords at +0x50/+0x58; writing 0 = "0 tiles"
        may hang firmware. buffer_build must write mt_ta_tile_pack(w)
        to +0x50 and mt_ta_tile_pack(h) to +0x58 (w/h approximate
        psKickTA[3]/[4] [INFERRED]).
        """
        body = _get_buffer_build_body()
        self.assertRegex(
            body,
            r"buf\s*\+\s*MT_TA_BUF_HDR_TILE_PACK_X\s*\)\s*=\s*mt_ta_tile_pack\s*\(\s*w\s*\)",
            "T5 FAIL: Header+0x50 not written from mt_ta_tile_pack(w) (r439)",
        )
        self.assertRegex(
            body,
            r"buf\s*\+\s*MT_TA_BUF_HDR_TILE_PACK_Y\s*\)\s*=\s*mt_ta_tile_pack\s*\(\s*h\s*\)",
            "T5 FAIL: Header+0x58 not written from mt_ta_tile_pack(h) (r439)",
        )

    def test_tile_pack_helper_defined(self):
        """T5 (r439): mt_ta_tile_pack must implement the UMD formula."""
        text = (KERNEL / "mt_ta_real.h").read_text()
        self.assertRegex(
            text,
            r"static inline u64 mt_ta_tile_pack\(u32 x\)",
            "mt_ta_tile_pack helper missing",
        )
        # Formula [MEASURED] (r438): (((x + 0x3f) >> 6) & 0x3f) << 48.
        self.assertIn("0x3fU", text)
        self.assertIn("<< 48", text)

    def test_header_write_whitelist(self):
        """T5 (r424, extended r439): only whitelisted Header offsets may be
        written in buffer_build.

        The 360B buffer is Header (0x00-0x160). Header-only mode (r423)
        permits writes only through named constants: MT_TA_BUF_HDR_TARGET_VA
        (+0x10, r423) and MT_TA_BUF_HDR_TILE_PACK_X/_Y (+0x50/+0x58, r439
        [MEASURED] UMD tile packing). Any other buf+offset write with a
        numeric offset below 0x160 risks Header pollution (r422 root-cause).
        Symbolic memset of the whole buffer is allowed.
        """
        body = _get_buffer_build_body()
        # Find writes of the form *(...) (buf + <something>) = ...
        # or buf[<something>] = ...
        writes = re.findall(
            r"(?:\*\(u(?:8|16|32|64) \*\)\s*)?\(\s*buf\s*\+\s*([^)]+)\)\s*=",
            body,
        )
        writes += re.findall(r"\bbuf\s*\[\s*([^\]]+)\s*\]\s*=", body)
        bad = []
        # Approved: the named Header constants (r423: +0x10; r439: +0x50/+0x58).
        approved = {
            "MT_TA_BUF_HDR_TARGET_VA",
            "MT_TA_BUF_HDR_TILE_PACK_X",
            "MT_TA_BUF_HDR_TILE_PACK_Y",
        }
        for w in writes:
            w = w.strip()
            if w in approved:
                continue
            # Numeric literal offset: must be >= 0x160 (outside Header).
            m = re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", w)
            if m:
                if int(w, 0) < 0x160:
                    bad.append(w)
            else:
                # Non-constant, non-approved expression: suspicious.
                bad.append(w)
        self.assertEqual(
            bad,
            [],
            "T5 FAIL: non-whitelisted Header write in buffer_build (r422): "
            + str(bad),
        )

    def test_submit_real_no_direct_buf_writes(self):
        """T5 (r424): mt_ta_submit_real must not write ta_buf directly.

        All Header bytes must come from mt_ta_real_buffer_build(); the
        submit path may only copy the built buffer via
        pvr_translator_bo_write. Direct ta_buf[...] writes would bypass
        the Header-only invariant.
        """
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        m = re.search(
            r"static int mt_ta_submit_real\(.*?\{", text, re.DOTALL
        )
        assert m, "mt_ta_submit_real not found"
        start = m.end()
        depth = 1
        i = start
        while depth > 0 and i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        body = text[start:i]
        hits = re.findall(r"\bta_buf\s*\[", body)
        self.assertEqual(
            hits,
            [],
            "T5 FAIL: direct ta_buf[] write in mt_ta_submit_real "
            "(must go via buffer_build):\n" + body,
        )


if __name__ == "__main__":
    unittest.main()
class TestRgnHeaderWiring(unittest.TestCase):
    """r431: TA Header +0x10 must come from the RgnHeader BO, not the
    pixel BO. r425 fed the raw 16KB pixel BO -> firmware parsed pixels
    as region headers -> 5s timeout. This pins the wiring."""

    def _bridge_src(self):
        return (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()

    def test_readback_uses_rgnheader_va(self):
        src = self._bridge_src()
        # pvr_cmd_ta_readback must feed rgnheader_va to the TA request.
        self.assertIn(
            "req.target_va = rctx->rgnheader_va;",
            src,
            "r431 FAIL: 0x82:0xFD must use rgnheader_va for TA Header+0x10 "
            "(r425: pixel BO hangs firmware)",
        )

    def test_readback_requires_rgnheader_ready(self):
        src = self._bridge_src()
        self.assertIn(
            "rgnheader_ready",
            src,
            "r431 FAIL: readback path must gate on rgnheader_ready",
        )

    def test_rgnheader_init_all_ones(self):
        """r434: create path must pre-fill RgnHeader per-dword with
        0x00000001 (InitRegionHeaderBuffer writes integer 1 per dword,
        [MEASURED] r433; corrects r431's memset-0xFF misread)."""
        src = self._bridge_src()
        self.assertIn(
            "rgn_dw[i] = MT_TA_RGNHEADER_INIT_DWORD;",
            src,
            "r434 FAIL: RgnHeader BO must be filled per-dword with "
            "0x00000001 (r433: integer 1, not 0xFF bytes)",
        )
        self.assertNotIn(
            "memset(rgn_init, 0xFF, sizeof(rgn_init));",
            src,
            "r434 FAIL: memset 0xFF is the r431 misread; per-dword "
            "integer 1 is correct (r433)",
        )

    def test_no_pixel_bo_for_header(self):
        """r431: the TA request must NOT take the pixel BO VA."""
        src = self._bridge_src()
        # Find pvr_cmd_ta_readback body and ensure it does not assign
        # req.target_va from rctx->target_va (the pixel BO).
        m = re.search(
            r"static int pvr_cmd_ta_readback.*?^}",
            src,
            re.M | re.S,
        )
        assert m, "pvr_cmd_ta_readback not found"
        body = m.group(0)
        self.assertNotIn(
            "req.target_va = rctx->target_va;",
            body,
            "r431 FAIL: TA Header+0x10 must not be the pixel BO VA (r425)",
        )
