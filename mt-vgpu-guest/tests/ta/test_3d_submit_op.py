#!/usr/bin/env python3
"""Gate the 3D submit op interface (r382, R3).

`kernel/mt_3d_submit.h` defines the 3D firmware submission channel:
opcode 0x68 (RGXCompute, r381), DM2, standard completion code 0, and
the MT_3D_SUBMIT_GATE (default 0 = disabled). This test compiles the
header in userspace and pins it, so the 3D submit path cannot drift
from the r381 research without failing the gate.
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
    printf("d3_complete=%u\n", MT_FW_3D_COMPLETE_CODE);
    printf("d3_opcode=%u\n", MT_FW_3D_OPCODE);
    printf("dm_3d=%u\n", MT_FW_DM_3D);
    printf("d3_gate=%u\n", MT_3D_SUBMIT_GATE);
    return 0;
}
"""

EXPECTED = {
    "d3_complete": 0,
    "d3_opcode": 0x68,
    "dm_3d": 2,
    "d3_gate": 0,
}


def compiled_values():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "d3_op.c"
        src.write_text(TU)
        binary = Path(tmp) / "d3_op"
        subprocess.run(
            ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT), str(src), "-o", str(binary)],
            check=True, capture_output=True, text=True,
        )
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    values = {}
    for line in out.strip().splitlines():
        k, v = line.split("=")
        values[k] = int(v)
    return values


class Test3dSubmitOp(unittest.TestCase):
    def test_3d_opcode_dm_gate(self):
        values = compiled_values()
        for key, want in EXPECTED.items():
            self.assertEqual(values[key], want, key)


if __name__ == "__main__":
    unittest.main()
