#!/usr/bin/env python3
"""Gate the TA submit op interface (r366, R2b phase 1).

`kernel/mt_ta_submit.h` now also defines the 0x66-class firmware completion
code (r365 measured). This test compiles the header in userspace and pins it,
so the TA-aware completion path cannot drift from the measured value without
failing the gate.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HEADER = ROOT / "kernel" / "mt_ta_submit.h"

TU = r"""
#include <stdio.h>
#include "kernel/mt_ta_submit.h"
int main(void)
{
    printf("ta_complete=%u\n", MT_FW_TA_COMPLETE_CODE);
    printf("ta_opcode=%u\n", MT_FW_TA_OPCODE);
    printf("dm_ta=%u\n", MT_FW_DM_TA);
    return 0;
}
"""

EXPECTED = {
    "ta_complete": 0x100,
    "ta_opcode": 0x66,
    "dm_ta": 3,
}


def compiled_values():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "ta_op.c"
        src.write_text(TU)
        binary = Path(tmp) / "ta_op"
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


class TestTaSubmitOp(unittest.TestCase):
    def test_ta_completion_code(self):
        values = compiled_values()
        for key, want in EXPECTED.items():
            self.assertEqual(values[key], want, key)


if __name__ == "__main__":
    unittest.main()
