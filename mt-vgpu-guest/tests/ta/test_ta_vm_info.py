"""r458: TA packet VM info (+0x18 root_pa, +0x20 token) tests.

Verifies:
1. struct mt_ta_submit_params has vm_root_pa/vm_token fields (mt_ta_submit.h)
2. mt_ta_submit_real fills them from exec_ctx_ta.process (mt_pvr_bridge.c)
3. mt_fw_ta_real_command takes root_pa/token and writes +0x18/+0x20 (mt_marker_fence.h)
4. mt_ta_submit_build passes params->vm_root_pa/vm_token through
5. 0x82:0xC path (mt_ta_params_from_musakickgfx2) leaves them 0 (observer-only)
"""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir

KERNEL = get_kernel_dir()


def _read(name):
    return (KERNEL / name).read_text()


class TestTaSubmitParamsVmFields(unittest.TestCase):
    """r458: struct mt_ta_submit_params has vm_root_pa/vm_token."""

    def test_vm_root_pa_field(self):
        text = _read("mt_ta_submit.h")
        # Field must be in struct mt_ta_submit_params
        m = re.search(
            r"struct mt_ta_submit_params \{([^}]+)\}", text, re.DOTALL
        )
        self.assertIsNotNone(m, "struct mt_ta_submit_params not found")
        body = m.group(1)
        self.assertIn("vm_root_pa", body)
        self.assertRegex(body, r"u64\s+vm_root_pa\s*;")

    def test_vm_token_field(self):
        text = _read("mt_ta_submit.h")
        m = re.search(
            r"struct mt_ta_submit_params \{([^}]+)\}", text, re.DOTALL
        )
        self.assertIsNotNone(m)
        body = m.group(1)
        self.assertIn("vm_token", body)
        self.assertRegex(body, r"u64\s+vm_token\s*;")

    def test_fields_after_check_fence(self):
        # r458: new fields appended after check_fence (ABI append, no reorder)
        text = _read("mt_ta_submit.h")
        m = re.search(
            r"struct mt_ta_submit_params \{([^}]+)\}", text, re.DOTALL
        )
        body = m.group(1)
        check_idx = body.find("check_fence")
        pa_idx = body.find("vm_root_pa")
        token_idx = body.find("vm_token")
        self.assertGreater(check_idx, 0)
        self.assertGreater(pa_idx, check_idx)
        self.assertGreater(token_idx, pa_idx)


class TestTaSubmitRealFillsVmInfo(unittest.TestCase):
    """r458: mt_ta_submit_real fills vm_root_pa/vm_token from exec_ctx_ta."""

    def _func_body(self):
        text = _read("recovery/mt_pvr_bridge.c")
        # Find the definition (not the forward declaration at line 5269).
        # Definition has __maybe_unused prefix.
        idx = text.find("__maybe_unused static int mt_ta_submit_real(")
        self.assertGreater(idx, 0, "mt_ta_submit_real definition not found")
        # Function is ~100 lines; take generous slice to cover assignments
        return text[idx : idx + 8000]

    def test_vm_root_pa_assignment(self):
        body = self._func_body()
        self.assertIn("work.params.vm_root_pa", body)

    def test_vm_token_assignment(self):
        body = self._func_body()
        self.assertIn("work.params.vm_token", body)

    def test_uses_exec_ctx_ta_process(self):
        # Must derive from rctx->exec_ctx_ta.process (same as
        # mt_execution_context_inputs)
        body = self._func_body()
        self.assertIn("rctx->exec_ctx_ta.process->vm->tables->backing.gpu_pa", body)
        self.assertIn("rctx->exec_ctx_ta.process->token", body)


class TestTaRealCommandWritesVmInfo(unittest.TestCase):
    """r458: mt_fw_ta_real_command writes +0x18/+0x20."""

    def _func_text(self):
        text = _read("mt_marker_fence.h")
        idx = text.find("mt_fw_ta_real_command(void *command")
        self.assertGreater(idx, 0, "mt_fw_ta_real_command not found")
        # Function is ~20 lines; take generous slice
        return text[idx : idx + 2000]

    def test_signature_has_root_pa_token(self):
        body = self._func_text()
        # Signature spans multiple lines; check params present
        sig_end = body.find("{")
        sig = body[:sig_end]
        self.assertIn("root_pa", sig)
        self.assertIn("token", sig)

    def test_writes_0x18(self):
        body = self._func_text()
        self.assertIn("mt_fw_put64(command, 0x18, root_pa)", body)

    def test_writes_0x20(self):
        body = self._func_text()
        self.assertIn("mt_fw_put64(command, 0x20, token)", body)


class TestTaSubmitBuildPassesThrough(unittest.TestCase):
    """r458: mt_ta_submit_build passes vm_root_pa/vm_token to command builder."""

    def test_passthrough(self):
        text = _read("mt_marker_fence.h")
        idx = text.find("static void mt_ta_submit_build(")
        self.assertGreater(idx, 0, "mt_ta_submit_build not found")
        body = text[idx : idx + 1500]
        self.assertIn("params->vm_root_pa", body)
        self.assertIn("params->vm_token", body)


class TestMusakickgfx2PathStaysZero(unittest.TestCase):
    """r458: 0x82:0xC observer path (mt_ta_params_from_musakickgfx2) leaves
    vm_root_pa/vm_token as 0 (uses {0} init, observer-only, no execution)."""

    def test_zero_init_covers_new_fields(self):
        text = _read("mt_ta_submit.h")
        idx = text.find("mt_ta_params_from_musakickgfx2(")
        self.assertGreater(idx, 0)
        body = text[idx : idx + 3000]
        # Must use {0} init which zeroes new fields
        self.assertIn("(struct mt_ta_submit_params){ 0 }", body)
        # Must NOT assign vm_root_pa/vm_token in this path
        # (find assignments after the {0} init)
        init_idx = body.find("(struct mt_ta_submit_params){ 0 }")
        after = body[init_idx:]
        self.assertNotIn("vm_root_pa =", after)
        self.assertNotIn("vm_token =", after)


if __name__ == "__main__":
    unittest.main()
