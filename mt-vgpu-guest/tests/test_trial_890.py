#!/usr/bin/env python3
"""r372: mt_trial_start must accept 0x890==2 (firmware pre-initialized at boot).

Reverse validation: reverting mt_fw_trial.h to `!= 0` only must fail this test.
"""
import re
import unittest
from pathlib import Path

REPO = Path("/opt/ydyun-vdesktop-linux-driver")
TRIAL_H = REPO / "mt-vgpu-guest/kernel/mt_fw_trial.h"


class TestTrial890Acceptance(unittest.TestCase):
    def setUp(self):
        self.src = TRIAL_H.read_text()

    def test_accepts_890_eq_2(self):
        """mt_trial_start entry check accepts reg890==2 (not just 0)."""
        # The entry check must allow both 0 and 2.
        self.assertRegex(
            self.src,
            r"reg890\s*!=\s*0\s*&&\s*reg890\s*!=\s*2",
            "mt_trial_start must accept 0x890==2 (firmware pre-initialized)",
        )

    def test_rejects_other_890_values(self):
        """Values other than 0/2 (e.g. 1, 3) must still be rejected."""
        # The check is `if (reg890 != 0 && reg890 != 2) return -EBUSY;`
        # This rejects 1, 3, and all other values.
        m = re.search(
            r"if\s*\(\s*reg890\s*!=\s*0\s*&&\s*reg890\s*!=\s*2\s*\)\s*\n?\s*return\s+-EBUSY",
            self.src,
        )
        self.assertIsNotNone(
            m, "mt_trial_start must return -EBUSY for 0x890 values other than 0/2"
        )

    def test_second_check_uses_entry_value(self):
        """Post-upload check compares against entry reg890, not hardcoded 0."""
        self.assertRegex(
            self.src,
            r"readl\(t->queue->registers \+ 0x890\) != reg890",
            "post-upload check must use entry reg890 value",
        )

    def test_fw_state_still_required(self):
        """Firmware state 0x898==1 is still required (not relaxed)."""
        self.assertIn(
            "mt_trial_fw_state(t) != 1",
            self.src,
            "firmware state check (0x898==1) must be preserved",
        )


if __name__ == "__main__":
    unittest.main()
