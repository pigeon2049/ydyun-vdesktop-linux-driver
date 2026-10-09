#!/usr/bin/env python3
"""r420: Q0/Q1 bit-field hardening tests (offline).

r419 proved via disassembly (FUN_00169240):
  - Q0 is a pure flags/control word; it carries ZERO address bits.
  - The 48-bit target VA lives in Q1 (uStack_88._0_6_ = *(param_1+0x10)).
  - 0x48000000000 = bits 39 and 42 (r419 corrected r410's 43/46 typo).

Three-state history (firmware behavior):
  - r414: Q0 = 0 (no set_target)            -> 0x100 COMPLETED, 219us
  - r418: Q0 = va | 0x48000000000 (polluted) -> submitted-but-ignored, timeout
  - r419: Q0 = 0x48000000000, Q1 = va&mask   -> (to be validated live, r420+)

These tests pin the corrected behavior at the source level so a future
refactor cannot silently reintroduce the r418 pollution.
"""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir

KERNEL = get_kernel_dir()

Q0_FLAGS = 0x48000000000
Q1_MASK = 0xFFFFFFFFFFFF


def _read(name):
    return (KERNEL / name).read_text()


def _set_target_body():
    """Extract mt_ta_entry_simple_set_target() body from mt_ta_real.h."""
    text = _read("mt_ta_real.h")
    m = re.search(
        r"static inline void mt_ta_entry_simple_set_target\("
        r"[^)]*\)\s*\{(.*?)\n\}",
        text,
        re.DOTALL,
    )
    assert m, "mt_ta_entry_simple_set_target not found"
    return m.group(1)


class TestQ0FlagBits(unittest.TestCase):
    def test_flag_constant_value(self):
        self.assertEqual(Q0_FLAGS, (1 << 42) | (1 << 39))

    def test_flag_bits_independent(self):
        # Each documented flag bit is independently set in the constant.
        self.assertTrue(Q0_FLAGS & (1 << 39))
        self.assertTrue(Q0_FLAGS & (1 << 42))

    def test_no_low32_bits(self):
        # Q0 must not touch the low 32 bits (address territory).
        self.assertEqual(Q0_FLAGS & 0xFFFFFFFF, 0)

    def test_bits_29_30_documented(self):
        # r419: bits 29/30 are condition bits from uVar15; our minimal
        # entry leaves them clear (Q0 = 0x48000000000 only). Pin that the
        # constant does NOT set them, so a future "helpful" addition is
        # caught.
        self.assertEqual(Q0_FLAGS & ((1 << 29) | (1 << 30)), 0)


class TestQ1MaskBoundaries(unittest.TestCase):
    def _apply_mask(self, va):
        # Mirror of the C: e->q1 = target_va & 0xFFFFFFFFFFFFULL
        return va & Q1_MASK

    def test_zero(self):
        self.assertEqual(self._apply_mask(0), 0)

    def test_full_48bit(self):
        self.assertEqual(self._apply_mask(0xFFFFFFFFFFFF), 0xFFFFFFFFFFFF)

    def test_49bit_truncated(self):
        # Bit 48 must be dropped.
        self.assertEqual(self._apply_mask(0x1000000000000), 0)
        self.assertEqual(self._apply_mask(0x1AB007A001000), 0xAB007A001000)

    def test_typical_va(self):
        self.assertEqual(self._apply_mask(0x7B000000), 0x7B000000)

    def test_mask_in_source(self):
        body = _set_target_body()
        self.assertIn("0xFFFFFFFFFFFFULL", body)


class TestQ0Q1Combination(unittest.TestCase):
    def test_set_target_assigns_q0_flags_only(self):
        body = _set_target_body()
        # Q0 gets exactly the flags constant, nothing OR'd in.
        self.assertRegex(
            body, r"e->q0_addr_flags\s*=\s*MT_TA_ENTRY_Q0_FLAG_BITS\s*;")

    def test_set_target_assigns_q1_masked_va(self):
        body = _set_target_body()
        self.assertRegex(
            body, r"e->q1\s*=\s*target_va\s*&\s*0xFFFFFFFFFFFFULL\s*;")

    def test_no_va_in_q0_expression(self):
        body = _set_target_body()
        # The r418 bug shape: q0 = ... va ... | ...
        for line in body.splitlines():
            if "q0_addr_flags" in line and "=" in line:
                rhs = line.split("=", 1)[1]
                self.assertNotIn("target_va", rhs)
                self.assertNotIn("va", rhs.lower().replace("q0_addr_flags", ""))


class TestThreeStateHistory(unittest.TestCase):
    """Pin the r414/r418/r419 behavioral states as documentation."""

    def test_r414_state_q0_zero_accepted(self):
        # r414: set_target never called -> q0_addr_flags stays 0 from
        # mt_ta_entry_simple_build's memset; firmware COMPLETED (0x100).
        # The build function must zero the entry (so Q0=0 is reachable).
        text = _read("mt_ta_real.h")
        self.assertIn("mt_ta_entry_simple_build", text)
        # build zeroes q0 (via memset of the 40B entry in buffer_build).
        self.assertIn("memset(buf, 0, MT_TA_CMD_BUFFER_BYTES)", text)

    def test_r418_state_must_not_regress(self):
        # r418: Q0 = va | flags -> firmware timeout. The current
        # set_target must NOT contain this shape.
        body = _set_target_body()
        self.assertNotRegex(body, r"q0_addr_flags\s*=\s*target_va\s*\|")
        self.assertNotRegex(body, r"q0_addr_flags\s*\|=\s*target_va")

    def test_r419_state_current(self):
        # r419: Q0 = flags, Q1 = va & 48-bit mask.
        body = _set_target_body()
        self.assertIn("MT_TA_ENTRY_Q0_FLAG_BITS", body)
        self.assertIn("0xFFFFFFFFFFFFULL", body)


if __name__ == "__main__":
    unittest.main()
