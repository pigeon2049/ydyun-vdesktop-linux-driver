"""r416: T2 readback verification tests."""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir, get_repo_root

KERNEL = get_kernel_dir()
REPO = get_repo_root()


def _read(name):
    return (KERNEL / name).read_text()


def _define_value(fname, name):
    text = _read(fname)
    m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+|\d+)" % name, text)
    assert m, "%s not defined in %s" % (name, fname)
    return int(m.group(1), 0)


class TestTaReadbackGate(unittest.TestCase):
    def test_readback_debug_default_off(self):
        # r416: MT_TA_READBACK_DEBUG must default to 0 (no debug ioctl).
        self.assertEqual(
            _define_value("mt_ta_real.h", "MT_TA_READBACK_DEBUG"), 0)

    def test_t2_target_constants(self):
        # r416: 64x64 RGBA8 render target.
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_WIDTH"), 64)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_HEIGHT"), 64)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_BYTES"),
                         64 * 64 * 4)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_BO_SLOT"), 11)

    def test_q0_flag_bits_inferred(self):
        # r416: Q0 flag bits from r410 ([INFERRED], TO-VALIDATE).
        text = _read("mt_ta_real.h")
        self.assertIn("MT_TA_ENTRY_Q0_FLAG_BITS", text)
        self.assertIn("[INFERRED]", text)
        self.assertEqual(
            _define_value("mt_ta_real.h", "MT_TA_ENTRY_Q0_FLAG_BITS"),
            0x48000000000)


class TestTaReadbackRequest(unittest.TestCase):
    def test_request_has_target_va(self):
        # r416: target_va added to mt_ta_real_request (0 = none).
        text = _read("mt_ta_real.h")
        self.assertIn("u64 target_va;", text)

    def test_q0_setter_exists(self):
        text = _read("mt_ta_real.h")
        self.assertIn("mt_ta_entry_simple_set_target", text)

    def test_buffer_build_takes_target(self):
        # r416: buffer builder accepts target_va.
        text = _read("mt_ta_real.h")
        m = re.search(
            r"mt_ta_real_buffer_build\(unsigned char \*buf, u32 w, u32 h,\s*\n?\s*u32 n_entries, u64 target_va\)",
            text)
        self.assertIsNotNone(m, "buffer_build must take target_va")


class TestTaReadbackKernel(unittest.TestCase):
    def test_bo_read_helper_exists(self):
        # r416: pvr_translator_bo_read mirrors bo_write (pre-existing).
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        self.assertIn("pvr_translator_bo_read", text)

    def test_render_context_target_fields(self):
        # r416: 12th BO fields on render context.
        text = _read("mt_render_context.h")
        self.assertIn("struct mt_bo target_bo;", text)
        self.assertIn("u64 target_va;", text)
        self.assertIn("bool target_ready;", text)

    def test_debug_ioctl_gated(self):
        # r416: 0xFD handler inside #if MT_TA_READBACK_DEBUG.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("pvr_cmd_ta_readback(struct mt_pvr_file")
        self.assertGreater(idx, 0)
        before = text[max(0, idx - 600):idx]
        self.assertIn("#if MT_TA_READBACK_DEBUG", before)

    def test_dispatch_case_gated(self):
        # r416: 0xFD dispatch case gated (uses MT_PVR_FN_DEBUGTAREADBACK macro).
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("case MT_PVR_FN_DEBUGTAREADBACK:")
        self.assertGreater(idx, 0)
        before = text[max(0, idx - 200):idx]
        self.assertIn("#if MT_TA_READBACK_DEBUG", before)

    def test_submit_real_passes_target(self):
        # r416: mt_ta_submit_real forwards req->target_va.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        self.assertIn("req->target_va", text)


class TestTaReadbackUserspace(unittest.TestCase):
    def test_tool_exists(self):
        p = REPO / "userspace" / "mt-ta-readback.c"
        self.assertTrue(p.exists(), "mt-ta-readback.c missing")

    def test_makefile_target(self):
        text = (REPO / "userspace" / "Makefile").read_text()
        self.assertIn("mt-ta-readback", text)

    def test_tool_writes_ppm(self):
        # r416: tool writes P6 PPM and verifies pixels.
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn('P6\\n', text)
        self.assertIn("nonzero_pixels", text)

    def test_tool_handles_enotty(self):
        # r416: tool degrades gracefully when debug ioctl absent.
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn("MT_TA_READBACK_DEBUG=1", text)
        self.assertIn("ENOTTY", text)


if __name__ == "__main__":
    unittest.main()
