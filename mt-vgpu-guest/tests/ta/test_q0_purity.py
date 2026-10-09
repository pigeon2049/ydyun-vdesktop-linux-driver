#!/usr/bin/env python3
"""r420 T4 gate: Q0 purity - firmware (dm, opcode) whitelist companion.

r418 postmortem: mt_ta_entry_simple_set_target() did
    e->q0_addr_flags = target_va | MT_TA_ENTRY_Q0_FLAG_BITS;
putting address bits into Q0 (a pure flags word). Firmware saw garbage
flags and hung (submitted-but-ignored, 5s timeout -> -ETIMEDOUT).
r414 (Q0=0) had completed in 219us. r419 disassembly proved Q0 carries
zero address bits; the 48-bit target VA belongs in Q1.

Rule enforced here: in-tree code must NEVER OR/add a VA/address into
Q0 (q0_addr_flags). Q0 is flags-only (MT_TA_ENTRY_Q0_FLAG_BITS).
The address goes in Q1, masked to 48 bits.

Reverse validation: change the constant assignment in
mt_ta_entry_simple_set_target() to `e->q0_addr_flags = target_va | ...`
-> test_q0_no_va_or_in must FAIL.
"""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir, get_repo_root

KERNEL = get_kernel_dir()

# Files that may construct TA entries.
SCAN_FILES = [
    "mt_ta_real.h",
    "mt_marker_fence.h",
]

# Forbidden: any OR/addition of a VA/address-like rvalue into q0.
# We scan the *statement* level: lines assigning to q0_addr_flags (or a
# local named q0) that mention va/addr/target on the RHS.
FORBIDDEN_RHS = re.compile(
    r"\bq0\w*\s*=[^;]*(\|\s*\w*(va|addr|target)\w*|"
    r"\+\s*\w*(va|addr|target)\w*|"
    r"\w*(va|addr|target)\w*\s*\|)",
    re.IGNORECASE,
)

# Allowed: the pure-flags constant assignment (possibly with the
# MT_TA_ENTRY_Q0_FLAG_BITS macro).
ALLOWED_RHS = re.compile(
    r"\bq0\w*\s*=\s*MT_TA_ENTRY_Q0_FLAG_BITS\s*;"
)


def _iter_q0_assignments(text):
    """Yield (lineno, line) for lines assigning to q0_addr_flags/q0."""
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        # Skip comments.
        if stripped.startswith("*") or stripped.startswith("//"):
            continue
        if re.search(r"\bq0\w*\s*=", line):
            yield i, line


class TestQ0Purity(unittest.TestCase):
    def test_q0_no_va_or_in(self):
        """T4: Q0 must never be OR'd with a VA/address (r418 lesson)."""
        hits = []
        for fname in SCAN_FILES:
            text = (KERNEL / fname).read_text()
            for lineno, line in _iter_q0_assignments(text):
                if ALLOWED_RHS.search(line):
                    continue
                if FORBIDDEN_RHS.search(line):
                    hits.append("%s:%d: %s" % (fname, lineno, line.strip()))
        self.assertEqual(
            hits, [],
            "T4 FAIL: Q0 polluted with VA/address (r418 re-run):\n"
            + "\n".join(hits),
        )

    def test_q0_constant_is_flags_only(self):
        """T4: MT_TA_ENTRY_Q0_FLAG_BITS touches only bits 39 and 42."""
        text = (KERNEL / "mt_ta_real.h").read_text()
        m = re.search(
            r"#define\s+MT_TA_ENTRY_Q0_FLAG_BITS\s+(0x[0-9a-fA-F]+)ULL", text
        )
        self.assertIsNotNone(m, "MT_TA_ENTRY_Q0_FLAG_BITS not defined")
        val = int(m.group(1), 16)
        # Bits 39 and 42 only (r419 corrected r410's 43/46 typo).
        self.assertEqual(val, (1 << 42) | (1 << 39))
        # No low-32 address bits.
        self.assertEqual(val & 0xFFFFFFFF, 0)

    def test_q1_gets_va(self):
        """T4: the 48-bit VA must be assigned to Q1 (not Q0)."""
        text = (KERNEL / "mt_ta_real.h").read_text()
        # set_target assigns q1 from target_va with 48-bit mask.
        self.assertRegex(
            text,
            r"e->q1\s*=\s*target_va\s*&\s*0xFFFFFFFFFFFFULL",
        )


if __name__ == "__main__":
    unittest.main()
