#!/usr/bin/env python3
"""Gate the 0x82:0xC IN -> mt_ta_submit_params mapping (r367, R2b).

Compiles the real `mt_ta_params_from_musakickgfx2()` from
kernel/mt_ta_submit.h in userspace and verifies the D5 mapping against
r363's measured IN values plus a TA-only variant. A wrong field mapping
fails the gate; reverse validation is done by mutating the C source.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "kernel" / "mt_ta_submit.h"

TU = r"""
#include <stdio.h>
#include <string.h>
#include "kernel/mt_pvr_wire.h"
#include "kernel/mt_ta_submit.h"

static void fill_r363(struct mt_pvr_musakickgfx2_in *in)
{
    memset(in, 0, sizeof(*in));
    /* r363 measured minimal kick: TA+PR, zero draws */
    in->kick_ta = 1;
    in->kick_pr = 1;
    in->kick_3d = 0;
    in->p_ta_cmd = 0x561392471410ULL;
    in->ta_cmd_size = 360;
    in->p_client_ta_upd_sync_off = 0x7ffd6cfb9030ULL;
    in->p_client_ta_upd_val = 0x7ffd6cfb9080ULL;
    in->ph_client_ta_upd_block = 0x7ffd6cfb9250ULL;
    in->client_ta_upd_count = 1;
    in->p_client_ta_fence_sync_off = 0x7ffd6cfb8f90ULL;
    in->p_client_ta_fence_val = 0x7ffd6cfb8fe0ULL;
    in->ph_client_ta_fence_block = 0x7ffd6cfb91c0ULL;
    in->client_ta_fence_count = 0;
    in->h_pr_fence_ufo_block = 0x102dULL;
    in->pr_fence_ufo_sync_offset = 0;
    in->pr_fence_value = 1;
    in->check_fence = 0;
}

int main(void)
{
    struct mt_pvr_musakickgfx2_in in;
    struct mt_ta_submit_params p;

    /* Case 1: r363 real kick */
    fill_r363(&in);
    mt_ta_params_from_musakickgfx2(&p, &in);
    printf("r363: kick_flags=%u ta_cmd_va=%llx ta_cmd_size=%u\n",
           p.kick_flags, (unsigned long long)p.ta_cmd_va, p.ta_cmd_size);
    printf("r363: upd_count=%u fence_count=%u check=%d\n",
           p.ta_upd_count, p.ta_fence_count, p.check_fence);
    printf("r363: pr_block=%llx pr_off=%u pr_val=%u\n",
           (unsigned long long)p.pr_fence_block, p.pr_fence_offset,
           p.pr_fence_value);

    /* Case 2: TA-only variant (gate-passing) */
    fill_r363(&in);
    in.kick_pr = 0;
    in.client_ta_upd_count = 0;
    mt_ta_params_from_musakickgfx2(&p, &in);
    printf("taonly: kick_flags=%u upd_count=%u\n",
           p.kick_flags, p.ta_upd_count);
    return 0;
}
"""


def compiled_output():
    assert HEADER.exists(), f"missing {HEADER}"
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "ta_map.c"
        src.write_text(TU)
        binary = Path(tmp) / "ta_map"
        subprocess.run(
            ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT), str(src), "-o", str(binary)],
            check=True, capture_output=True, text=True,
        )
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    return out


class TestTaKickDispatchMapping(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = compiled_output()

    def test_r363_kick_flags(self):
        # TA+PR kick must set both bits (D5)
        self.assertIn("r363: kick_flags=3", self.out)

    def test_r363_ta_cmd(self):
        self.assertIn("ta_cmd_va=561392471410 ta_cmd_size=360", self.out)

    def test_r363_counts(self):
        # ta_upd_count=1 -> D8 honest refusal path; fence_count=0
        self.assertIn("r363: upd_count=1 fence_count=0 check=0", self.out)

    def test_r363_pr_fence(self):
        self.assertIn("r363: pr_block=102d pr_off=0 pr_val=1", self.out)

    def test_ta_only_kick_flags(self):
        # TA-only must set just the TA bit
        self.assertIn("taonly: kick_flags=1 upd_count=0", self.out)


if __name__ == "__main__":
    unittest.main()
