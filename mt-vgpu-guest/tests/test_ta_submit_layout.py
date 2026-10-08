#!/usr/bin/env python3
"""Gate the TA submission channel interface (r364, R4 design).

`kernel/mt_ta_submit.h` defines the probe-side TA submit interface: the
TA data-master assignment, the TA command opcode candidate, and struct
mt_ta_submit_params (the decoded 0x82:0xC IN subset the submit path
needs). This test compiles the header in userspace and pins
sizes/offsets/constants, so the R2b implementation cannot drift from
the r364 design without failing the gate.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "kernel" / "mt_ta_submit.h"

TU = r"""
#include <stdio.h>
#include "kernel/mt_ta_submit.h"
int main(void)
{
    printf("sizeof_params=%zu\n", sizeof(struct mt_ta_submit_params));
    printf("off_ta_cmd_va=%zu\n",
           __builtin_offsetof(struct mt_ta_submit_params, ta_cmd_va));
    printf("off_ta_upd_count=%zu\n",
           __builtin_offsetof(struct mt_ta_submit_params, ta_upd_count));
    printf("off_check_fence=%zu\n",
           __builtin_offsetof(struct mt_ta_submit_params, check_fence));
    printf("dm_ta=%u\n", MT_FW_DM_TA);
    printf("opcode=%u\n", MT_FW_TA_OPCODE);
    printf("kick_ta=%u\n", MT_TA_KICK_TA);
    printf("kick_pr=%u\n", MT_TA_KICK_PR);
    return 0;
}
"""

# Design pins from kernel/mt_ta_submit.h (r364).
EXPECTED = {
    "sizeof_params": 104,
    "off_ta_cmd_va": 0,
    "off_ta_upd_count": 40,
    "off_check_fence": 96,
    "dm_ta": 3,
    "opcode": 0x66,
    "kick_ta": 1,
    "kick_pr": 2,
}


def compiled_values():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "ta_layout.c"
        src.write_text(TU)
        binary = Path(tmp) / "ta_layout"
        subprocess.run(
            ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT), str(src), "-o", str(binary)],
            check=True, capture_output=True, text=True)
        out = subprocess.run([str(binary)], check=True,
                             capture_output=True, text=True)
    values = {}
    for line in out.stdout.splitlines():
        key, val = line.split("=")
        values[key] = int(val)
    return values


class TaSubmitLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.values = compiled_values()

    def test_layout_matches_design(self):
        for key, want in EXPECTED.items():
            self.assertEqual(self.values[key], want, key)

    def test_dm_does_not_collide_with_live_dms(self):
        dm = self.values["dm_ta"]
        self.assertGreaterEqual(dm, 1)
        self.assertLess(dm, 6)
        # 1 = TQX/transfer, 2 = 3D Universal (live code); 0 = META/system.
        self.assertNotIn(dm, (0, 1, 2))


if __name__ == "__main__":
    unittest.main()
