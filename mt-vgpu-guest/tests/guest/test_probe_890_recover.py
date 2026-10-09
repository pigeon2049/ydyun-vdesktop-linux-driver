#!/usr/bin/env python3
"""r446: mt_guest_probe must accept 0x890==2 on recover_channels path.

Defect 2 (r445): recover_channels=1 expected reg890==1, but firmware may
boot with the session indicator set (reg890==2); cold boot does not clear
it. Only trial_connect accepted reg890==2. The fix extends the exception
to recover_channels.

Defect 1 (r445): recover_channels=1 + reserve_memory=1 is an illegal
combination, rejected with -EINVAL. The test program (r443/r444) used it
by mistake. This test locks in the rejection.

Reverse validation: reverting kernel/mt_guest_probe.c to the r445 version
must fail these tests.
"""
import re
import unittest
from pathlib import Path

REPO = Path("/opt/ydyun-vdesktop-linux-driver")
PROBE_C = REPO / "mt-vgpu-guest/kernel/mt_guest_probe.c"


class TestRecoverChannels890Acceptance(unittest.TestCase):
    def setUp(self):
        self.src = PROBE_C.read_text()

    def test_mt_probe_accepts_890_eq_2_on_recover(self):
        """mt_probe: reg890==2 accepted when recover_channels=1."""
        self.assertRegex(
            self.src,
            r"reg890 == 2 && \(trial_connect \|\| recover_channels\)",
            "mt_probe must accept 0x890==2 on recover_channels path",
        )

    def test_mt_probe_comment_mentions_recover_channels(self):
        """mt_probe comment documents the recover_channels exception."""
        self.assertRegex(
            self.src,
            r"accepted when trial_connect or recover_channels",
            "mt_probe comment must document recover_channels exception",
        )

    def test_read_device_info_accepts_890_eq_2_on_recover(self):
        """mt_read_device_info: reg890==2 accepted when recover_channels=1."""
        # The same pattern appears in mt_read_device_info and mt_probe;
        # require at least 2 occurrences (both functions fixed).
        matches = re.findall(
            r"reg890 == 2 && \(trial_connect \|\| recover_channels\)", self.src
        )
        self.assertGreaterEqual(
            len(matches), 2,
            "mt_read_device_info and mt_probe must both accept 0x890==2 "
            "on recover_channels path",
        )

    def test_snapshot_memory_accepts_890_eq_2(self):
        """mt_snapshot_memory: reg890==2 accepted (consistency)."""
        self.assertRegex(
            self.src,
            r"reg890_snap != \(recover_channels \? 1 : 0\) && reg890_snap != 2",
            "mt_snapshot_memory must accept 0x890==2",
        )

    def test_old_trial_connect_only_pattern_gone(self):
        """The old trial_connect-only exception must not remain."""
        self.assertNotRegex(
            self.src,
            r"!\(trial_connect && !recover_channels && reg890 == 2\)",
            "old trial_connect-only exception must be replaced",
        )


class TestIllegalParamComboRejected(unittest.TestCase):
    def setUp(self):
        self.src = PROBE_C.read_text()

    def test_recover_plus_reserve_memory_rejected(self):
        """recover_channels=1 + reserve_memory=1 must be rejected (-EINVAL)."""
        # The probe parameter validation rejects this illegal combination.
        m = re.search(
            r"if \(recover_channels && \([^)]*reserve_memory[^)]*\)\)\s*\n\s*return -EINVAL;",
            self.src,
        )
        self.assertIsNotNone(
            m,
            "mt_probe must reject recover_channels=1 + reserve_memory=1 "
            "with -EINVAL",
        )

    def test_recover_plus_trial_connect_rejected(self):
        """recover_channels=1 + trial_connect=1 must be rejected (-EINVAL)."""
        m = re.search(
            r"if \(recover_channels && \([^)]*trial_connect[^)]*\)\)\s*\n\s*return -EINVAL;",
            self.src,
        )
        self.assertIsNotNone(
            m,
            "mt_probe must reject recover_channels=1 + trial_connect=1 "
            "with -EINVAL",
        )


if __name__ == "__main__":
    unittest.main()
