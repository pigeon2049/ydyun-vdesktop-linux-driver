"""r411: Real TA packet infrastructure tests."""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir

KERNEL = get_kernel_dir()


def _read(name):
    return (KERNEL / name).read_text()


def _define_value(fname, name):
    text = _read(fname)
    m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+|\d+)" % name, text)
    assert m, "%s not defined in %s" % (name, fname)
    return int(m.group(1), 0)


class TestTaRealPacketGate(unittest.TestCase):
    def test_gate_default_off(self):
        # r411: MT_TA_REAL_PACKET must default to 0 (marker path active).
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_REAL_PACKET"), 0)

    def test_cmd_buffer_size(self):
        # r410 [MEASURED]: 0x168=360 immediate in RGXSubmitTA.
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_CMD_BUFFER_BYTES"), 0x168)

    def test_entry_simple_size(self):
        # r410 [MEASURED]: 5 qwords = 40B.
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_ENTRY_SIMPLE_BYTES"), 40)

    def test_entry_builder_exists(self):
        text = _read("mt_ta_real.h")
        self.assertIn("mt_ta_entry_simple_build", text)

    def test_real_command_gated(self):
        # r411: mt_fw_ta_real_command must be inside #if MT_TA_REAL_PACKET.
        text = _read("mt_marker_fence.h")
        idx = text.find("mt_fw_ta_real_command(void *command")
        self.assertGreater(idx, 0)
        # Find enclosing #if
        before = text[max(0, idx - 500):idx]
        self.assertIn("#if MT_TA_REAL_PACKET", before)

    def test_marker_path_preserved(self):
        # r411: marker builder still present and called when gate off.
        text = _read("mt_marker_fence.h")
        self.assertIn("mt_fw_ta_marker_command(packet, wire_id, pid)", text)


class TestTaRealProductization(unittest.TestCase):
    """r415: production real-TA path (DM layout [MEASURED], mt_ta_submit_real)."""

    def test_dm_offsets_measured(self):
        # r415: VA@+0x28/size@+0x30 promoted [INFERRED] -> [MEASURED] (r414).
        text = _read("mt_ta_real.h")
        self.assertIn("[MEASURED] (r414 live)", text)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_DM_PKT_TA_VA_LO"), 0x28)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_DM_PKT_TA_SIZE"), 0x30)

    def test_staging_constants(self):
        # r415: BO[10]@4096 staging location, [MEASURED] (r414).
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_REAL_STAGING_BO_INDEX"), 10)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_REAL_STAGING_BO_OFFSET"), 4096)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_TA_REAL_MAX_ENTRIES"), 9)

    def test_buffer_builder_exists(self):
        # r415: pure buffer builder (parameterized, unit-testable).
        text = _read("mt_ta_real.h")
        self.assertIn("mt_ta_real_buffer_build", text)
        self.assertIn("n_entries", text)

    def test_request_struct_parameterized(self):
        # r415: request struct has width/height/n_entries (no hardcode).
        text = _read("mt_ta_real.h")
        self.assertIn("struct mt_ta_real_request", text)
        self.assertIn("u32 width;", text)
        self.assertIn("u32 height;", text)
        self.assertIn("u32 n_entries;", text)

    def test_submit_real_in_bridge(self):
        # r415: mt_ta_submit_real exists in bridge, gated.
        text = _read("recovery/mt_pvr_bridge.c")
        self.assertIn("mt_ta_submit_real", text)
        # r417: anchor on the definition (__maybe_unused); a forward
        # declaration precedes the 0xFD handler.
        idx = text.find("__maybe_unused static int "
                        "mt_ta_submit_real(struct mt_pvr_file *file,")
        self.assertGreater(idx, 0)
        before = text[max(0, idx - 2000):idx]
        self.assertIn("#if MT_TA_REAL_PACKET", before)

    def test_submit_real_no_hardcode(self):
        # r415: production function takes req params, no hardcoded 64x64.
        text = _read("recovery/mt_pvr_bridge.c")
        # r417: anchor on the definition, not the forward decl.
        idx = text.find("__maybe_unused static int "
                        "mt_ta_submit_real(struct mt_pvr_file *file,")
        # Find function body (next 3000 chars)
        body = text[idx:idx + 3000]
        self.assertIn("req->width", body)
        self.assertIn("req->height", body)
        self.assertIn("req->n_entries", body)
        # No hardcoded 64, 64 from the test hook
        self.assertNotIn("mt_ta_entry_simple_build((struct mt_ta_entry_simple *)ta_buf,\n\t\t\t\t\t       64, 64)", body)

    def test_fe_hook_gone(self):
        # r415: 0xFE test hook must NOT be in production bridge.
        text = _read("recovery/mt_pvr_bridge.c")
        self.assertNotIn("case 0xFE:", text)
        self.assertNotIn("pvr_cmd_ta_real_test", text)

    def test_gate_process_documented(self):
        # r415: gate opening process documented in header.
        text = _read("mt_ta_real.h")
        self.assertIn("gate opening process", text.lower())
        self.assertIn("Rollback", text)



if __name__ == "__main__":
    unittest.main()


class TestTaDmLayoutUsage(unittest.TestCase):
    """r420: DM layout [MEASURED] regression - constants must be USED."""

    def test_va_lo_used_in_real_command(self):
        # r414 [MEASURED]: mt_fw_ta_real_command must write VA via
        # MT_TA_DM_PKT_TA_VA_LO (0x28), not a hardcoded literal.
        text = _read("mt_marker_fence.h")
        self.assertIn("MT_TA_DM_PKT_TA_VA_LO", text)
        # The constant must appear inside mt_fw_ta_real_command.
        idx = text.find("mt_fw_ta_real_command(void *command")
        self.assertGreater(idx, 0)
        body = text[idx:idx + 800]
        self.assertIn("MT_TA_DM_PKT_TA_VA_LO", body)
        self.assertIn("MT_TA_DM_PKT_TA_VA_HI", body)
        self.assertIn("MT_TA_DM_PKT_TA_SIZE", body)

    def test_no_hardcoded_0x28_in_ta_real(self):
        # r420: the TA real command builder must not hardcode 0x28/0x30;
        # it must use the named constants (so a layout change is caught).
        text = _read("mt_marker_fence.h")
        idx = text.find("mt_fw_ta_real_command(void *command")
        self.assertGreater(idx, 0)
        body = text[idx:idx + 800]
        # No raw 0x28/0x2c/0x30 offsets in the TA real builder.
        import re
        for m in re.finditer(r"mt_fw_put32\(command,\s*(0x[0-9a-fA-F]+)", body):
            off = m.group(1)
            self.assertNotIn(
                off, ("0x28", "0x2c", "0x30"),
                "hardcoded DM offset %s in mt_fw_ta_real_command; "
                "use MT_TA_DM_PKT_* constants" % off,
            )

    def test_fence_comment_measured(self):
        # r420: the stale [INFERRED] comment in mt_marker_fence.h must
        # reflect r414's live validation.
        text = _read("mt_marker_fence.h")
        # Find the Real TA command packet comment block.
        idx = text.find("Real TA command packet (r411)")
        self.assertGreater(idx, 0)
        block = text[max(0, idx - 200):idx + 400]
        self.assertNotIn("[INFERRED]: TA buffer VA", block)
        self.assertIn("[MEASURED]", block)
