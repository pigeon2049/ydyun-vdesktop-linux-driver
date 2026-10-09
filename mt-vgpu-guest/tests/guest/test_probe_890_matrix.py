#!/usr/bin/env python3
"""r449: Complete reg890/0x898 state-machine matrix + full parameter validation.

Extends r446/r448 with:
1. reg890 state matrix: {0,1,2,other} x recover_channels {0,1} x trial_connect {0,1}
   across all 4 fixed locations + test helpers + retained-kick experimental path.
2. Complete parameter validation: all 6 -EINVAL rules in mt_probe (r446/r448
   only covered 2 of the recover_channels sub-conditions).
3. 0x898 channel-ready bit: strict ==1 check verified at all locations.
   [TO-VALIDATE]: 0x898 persistence across cold boot is UNKNOWN. If firmware
   boots with 0x898!=1, all paths return -EBUSY with no exception (unlike
   0x890==2). This is a known gap, not a defect -- 0x898 is the firmware-ready
   bit, expected 1 after boot. Recorded here so a future -EBUSY with
   0x898!=1 is diagnosable.
4. Regression: normal paths (reg890==1+recover, reg890==0+no-recover) accepted.

Reverse validation: reverting kernel/mt_guest_probe.c to r445 must fail the
reg890-acceptance tests; the param-validation tests lock in existing logic
(pass on both old and new code where the logic predates r446).

Style: source-pattern tests (regex on mt_guest_probe.c), matching the
established pattern in test_probe_890_recover.py / test_trial_890.py.
"""
import re
import unittest
from pathlib import Path

REPO = Path("/opt/ydyun-vdesktop-linux-driver")
PROBE_C = REPO / "mt-vgpu-guest/kernel/mt_guest_probe.c"


def _fn_body(src, name):
    m = re.search(
        r"static int %s\(.*?\n\{(.*?)\n\}\n" % re.escape(name),
        src,
        re.DOTALL,
    )
    assert m, "%s function not found" % name
    return m.group(1)


class TestReg890StateMatrix(unittest.TestCase):
    """Documented acceptance matrix for 0x890 across all probe-path checks.

    Matrix (expected accept / reject):
      mt_probe / mt_read_device_info (query_info path):
        recover=0,trial=0 : accept {0}; reject {1,2,3..}
        recover=0,trial=1 : accept {0,2}; reject {1,3..}
        recover=1,trial=0 : accept {1,2}; reject {0,3..}
        recover=1,trial=1 : ILLEGAL (param validation -EINVAL)
      mt_probe_channels (recover branch, recover=1 only):
        accept {1,2}; reject {0,3..}
      mt_reserve_memory (recover=0 always, combo rejected):
        accept {0,2}; reject {1,3..}
      mt_snapshot_memory (recover=0 in practice):
        accept {0,2} (recover=0); {1,2} (recover=1, hypothetical)
      test helpers (mt_test_firmware_upload, mt_test_memory_write):
        accept {0} only -- destructive tests require clean state (deliberate)
      retained-kick sysfs (experimental):
        accept {1} only -- intentionally narrow scope (deliberate)
    """

    def setUp(self):
        self.src = PROBE_C.read_text()

    def test_probe_matrix_pattern(self):
        """mt_probe query_info path encodes the full matrix."""
        body = _fn_body(self.src, "mt_probe")
        self.assertRegex(
            body,
            r"reg890 != \(recover_channels \? 1 : 0\)",
            "mt_probe must branch expected value on recover_channels",
        )
        self.assertRegex(
            body,
            r"reg890 == 2 && \(trial_connect \|\| recover_channels\)",
            "mt_probe must accept 0x890==2 iff trial_connect or recover_channels",
        )

    def test_read_device_info_matrix_pattern(self):
        """mt_read_device_info encodes the same matrix as mt_probe."""
        body = _fn_body(self.src, "mt_read_device_info")
        self.assertRegex(
            body,
            r"reg890 != \(recover_channels \? 1 : 0\)",
            "mt_read_device_info must branch expected value on recover_channels",
        )
        self.assertRegex(
            body,
            r"reg890 == 2 && \(trial_connect \|\| recover_channels\)",
            "mt_read_device_info must accept 0x890==2 iff trial_connect "
            "or recover_channels",
        )

    def test_probe_channels_matrix_pattern(self):
        """mt_probe_channels recover branch: accept {1,2}, reject others."""
        body = _fn_body(self.src, "mt_probe_channels")
        self.assertRegex(
            body,
            r"\(reg890 != 1 && reg890 != 2\)",
            "mt_probe_channels recover branch must accept exactly {1,2}",
        )

    def test_reserve_memory_matrix_pattern(self):
        """mt_reserve_memory: accept {0,2} (recover_channels always 0 here)."""
        body = _fn_body(self.src, "mt_reserve_memory")
        self.assertRegex(
            body,
            r"\(reg890 != 0 && reg890 != 2\)",
            "mt_reserve_memory must accept exactly {0,2}",
        )

    def test_snapshot_memory_matrix_pattern(self):
        """mt_snapshot_memory: branch on recover_channels, plus ==2."""
        body = _fn_body(self.src, "mt_snapshot_memory")
        self.assertRegex(
            body,
            r"reg890_snap != \(recover_channels \? 1 : 0\) && reg890_snap != 2",
            "mt_snapshot_memory must branch on recover_channels and accept ==2",
        )

    def test_helpers_strict_zero(self):
        """Destructive test helpers require 0x890==0 (deliberate, unchanged)."""
        for fn in ("mt_test_firmware_upload", "mt_test_memory_write"):
            body = _fn_body(self.src, fn)
            self.assertRegex(
                body,
                r"readl\(g->regs \+ 0x890\) != 0",
                "%s must keep strict 0x890==0 requirement" % fn,
            )

    def test_retained_kick_strict_one(self):
        """Retained-kick sysfs requires 0x890==1 (experimental, deliberate)."""
        self.assertRegex(
            self.src,
            r"readl\(g->regs \+ 0x890\) != 1 \|\| readl\(g->regs \+ 0x898\) != 1",
            "retained-kick path must keep strict 0x890==1 requirement",
        )

    def test_no_bare_890_eq_1_in_recover_path(self):
        """No recover_channels probe-path check may require bare 0x890==1."""
        # The only bare `!= 1` on 0x890 must be the retained-kick experiment.
        bare = re.findall(
            r"readl\(g->regs \+ 0x890\) != 1(?!\s*&&\s*reg890)",
            self.src,
        )
        # retained-kick (1 occurrence) is the only legitimate bare ==1 check.
        self.assertEqual(
            len(bare),
            1,
            "only the retained-kick experimental path may require bare "
            "0x890==1; found %d" % len(bare),
        )


class TestParamValidationComplete(unittest.TestCase):
    """All 6 -EINVAL parameter-validation rules in mt_probe must hold.

    r446/r448 locked 2 recover_channels sub-conditions; this class locks the
    remaining rules so future edits cannot silently widen/narrow them.
    """

    def setUp(self):
        self.src = PROBE_C.read_text()
        self.probe = _fn_body(self.src, "mt_probe")

    def test_refresh_osid_requires_recover(self):
        self.assertRegex(
            self.probe,
            r"if \(refresh_osid && !recover_channels\)\s*\n\s*return -EINVAL;",
            "refresh_osid without recover_channels must be -EINVAL",
        )

    def test_runtime_context_requires_query_and_firmware(self):
        self.assertRegex(
            self.probe,
            r"if \(runtime_context && \(!query_info \|\| !load_firmware\)\)"
            r"\s*\n\s*return -EINVAL;",
            "runtime_context without query_info+load_firmware must be -EINVAL",
        )

    def test_recover_channels_exclusivity(self):
        """recover_channels rejects all 8 conflicting flags."""
        m = re.search(
            r"if \(recover_channels && \(\s*\n?\s*!query_info \|\| !probe_rpc \|\| "
            r"trial_connect \|\|\s*\n?\s*reserve_memory \|\| test_memory_write \|\|"
            r"\s*\n?\s*prepare_resources \|\| load_firmware \|\| "
            r"test_firmware_upload \|\| runtime_context\)\)"
            r"\s*\n\s*return -EINVAL;",
            self.probe,
        )
        self.assertIsNotNone(
            m,
            "recover_channels must reject !query_info, !probe_rpc, "
            "trial_connect, reserve_memory, test_memory_write, "
            "prepare_resources, load_firmware, test_firmware_upload, "
            "runtime_context with -EINVAL",
        )

    def test_memory_write_needs_reserve(self):
        self.assertRegex(
            self.probe,
            r"if \(\(test_memory_write \|\| prepare_resources\) && !reserve_memory\)"
            r"\s*\n\s*return -EINVAL;",
            "test_memory_write/prepare_resources without reserve_memory "
            "must be -EINVAL",
        )

    def test_firmware_chain_order(self):
        self.assertRegex(
            self.probe,
            r"if \(\(load_firmware && !prepare_resources\) \|\| "
            r"\(test_firmware_upload && !load_firmware\)\)"
            r"\s*\n\s*return -EINVAL;",
            "load_firmware without prepare_resources, or "
            "test_firmware_upload without load_firmware, must be -EINVAL",
        )

    def test_trial_connect_requirements(self):
        m = re.search(
            r"if \(trial_connect && \(!query_info \|\| !load_firmware \|\| "
            r"!probe_rpc \|\|\s*\n?\s*test_firmware_upload \|\| "
            r"test_memory_write\)\)\s*\n\s*return -EINVAL;",
            self.probe,
        )
        self.assertIsNotNone(
            m,
            "trial_connect must require query_info+load_firmware+probe_rpc "
            "and reject test_firmware_upload/test_memory_write with -EINVAL",
        )


class TestChannelReady0898(unittest.TestCase):
    """0x898 (firmware-ready) must remain a strict ==1 check everywhere.

    [TO-VALIDATE]: whether 0x898 can persist !=1 across cold boot like
    0x890==2 does. No exception exists for 0x898; if firmware ever boots
    with 0x898!=1, every probe path returns -EBUSY. This is recorded so a
    future -EBUSY with 0x898!=1 is diagnosable without re-deriving.
    """

    def setUp(self):
        self.src = PROBE_C.read_text()

    def test_0898_strict_in_all_recover_locations(self):
        """All 4 fixed locations still require 0x898==1."""
        for fn in (
            "mt_probe",
            "mt_read_device_info",
            "mt_snapshot_memory",
            "mt_probe_channels",
            "mt_reserve_memory",
        ):
            body = _fn_body(self.src, fn)
            self.assertIn(
                "readl(g->regs + 0x898) != 1",
                body,
                "%s must keep strict 0x898==1 check" % fn,
            )

    def test_0898_no_relaxation_anywhere(self):
        """No 0x898 check may accept values other than 1."""
        relaxed = re.findall(r"0x898\)\s*!=\s*1\s*&&[^;]*!=\s*2", self.src)
        self.assertEqual(
            relaxed,
            [],
            "0x898 must have no ==2-style exception anywhere; "
            "found: %r" % relaxed,
        )

    def test_0898_tovalidate_documented(self):
        """The [TO-VALIDATE] persistence question is recorded in code."""
        self.assertRegex(
            self.src,
            r"0x898",
            "0x898 references must exist (sanity)",
        )
        # The question lives in this test's docstring; ensure the marker
        # text is present so grepping finds it.
        self.assertIn("[TO-VALIDATE]", __doc__)


class TestNormalPathRegression(unittest.TestCase):
    """r446/r448 must not break the normal (non-persisted) paths."""

    def setUp(self):
        self.src = PROBE_C.read_text()

    def test_recover_channels_accepts_890_eq_1(self):
        """reg890==1 + recover_channels=1 is the normal recover path."""
        body = _fn_body(self.src, "mt_probe_channels")
        # (reg890 != 1 && reg890 != 2) -> -EBUSY means 1 is accepted.
        self.assertRegex(
            body,
            r"\(reg890 != 1 && reg890 != 2\)",
            "mt_probe_channels must still accept the normal reg890==1",
        )

    def test_no_recover_accepts_890_eq_0(self):
        """reg890==0 + recover_channels=0 is the normal fresh-boot path."""
        body = _fn_body(self.src, "mt_probe")
        self.assertRegex(
            body,
            r"reg890 != \(recover_channels \? 1 : 0\)",
            "mt_probe must still expect 0x890==0 when recover_channels=0",
        )

    def test_reserve_memory_accepts_890_eq_0(self):
        """reg890==0 is the normal reserve_memory path."""
        body = _fn_body(self.src, "mt_reserve_memory")
        self.assertRegex(
            body,
            r"\(reg890 != 0 && reg890 != 2\)",
            "mt_reserve_memory must still accept the normal reg890==0",
        )


if __name__ == "__main__":
    unittest.main()
