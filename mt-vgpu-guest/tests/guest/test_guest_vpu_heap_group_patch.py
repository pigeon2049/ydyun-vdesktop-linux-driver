import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "scripts" / "patch-guest-vpu-heap-group.py"
SPEC = importlib.util.spec_from_file_location("guest_vpu_heap_group_patch", SCRIPT)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class GuestVpuHeapGroupPatchTest(unittest.TestCase):
    def test_changes_only_the_guarded_group_usage_immediate(self):
        body = bytearray(PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_OLD) + 16)
        body[PATCH.VPU_CONFIG_OFFSET:PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_OLD)] = PATCH.VPU_CONFIG_OLD

        patched = PATCH.patch_body(bytes(body))

        self.assertEqual(
            patched[PATCH.VPU_CONFIG_OFFSET:PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_NEW)],
            PATCH.VPU_CONFIG_NEW,
        )
        self.assertEqual(
            patched[:PATCH.VPU_CONFIG_OFFSET],
            bytes(body[:PATCH.VPU_CONFIG_OFFSET]),
        )
        self.assertEqual(
            patched[PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_NEW):],
            bytes(body[PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_OLD):]),
        )

    def test_refuses_an_unrecognized_instruction(self):
        body = bytearray(PATCH.VPU_CONFIG_OFFSET + len(PATCH.VPU_CONFIG_OLD))
        with self.assertRaises(ValueError):
            PATCH.patch_body(bytes(body))


if __name__ == "__main__":
    unittest.main()
