#!/usr/bin/env python3
"""Gate the 3D submission params layout (r382, R3).

`kernel/mt_3d_submit.h` defines struct mt_3d_submit_params (the decoded
0x82:0x14 IN subset the submit path needs). This test compiles the header
in userspace and pins sizes/offsets/constants, so the R3 implementation
cannot drift from the r382 design without failing the gate.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
HEADER = ROOT / "kernel" / "mt_3d_submit.h"

TU = r"""
#include <stdio.h>
#include "kernel/mt_3d_submit.h"
int main(void)
{
    printf("sizeof_params=%zu\n", sizeof(struct mt_3d_submit_params));
    printf("off_submission_va=%zu\n",
           __builtin_offsetof(struct mt_3d_submit_params, submission_va));
    printf("off_submission_size=%zu\n",
           __builtin_offsetof(struct mt_3d_submit_params, submission_size));
    printf("off_check_fence=%zu\n",
           __builtin_offsetof(struct mt_3d_submit_params, check_fence));
    printf("off_vm_map_hook=%zu\n",
           __builtin_offsetof(struct mt_3d_submit_params, vm_map_hook));
    printf("dm_3d=%u\n", MT_FW_DM_3D);
    printf("opcode=%u\n", MT_FW_3D_OPCODE);
    printf("gate=%u\n", MT_3D_SUBMIT_GATE);
    return 0;
}
"""

# Design pins from kernel/mt_3d_submit.h (r382).
EXPECTED = {
    "sizeof_params": 56,
    "off_submission_va": 0,
    "off_submission_size": 24,
    "off_check_fence": 44,
    "off_vm_map_hook": 48,
    "dm_3d": 2,
    "opcode": 0x68,
    "gate": 0,
}


def compiled_values():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "d3_layout.c"
        src.write_text(TU)
        binary = Path(tmp) / "d3_layout"
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


class D3SubmitLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.values = compiled_values()

    def test_layout_matches_design(self):
        for key, want in EXPECTED.items():
            self.assertEqual(self.values[key], want, key)


if __name__ == "__main__":
    unittest.main()
