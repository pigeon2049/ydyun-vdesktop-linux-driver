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


if __name__ == "__main__":
    unittest.main()
