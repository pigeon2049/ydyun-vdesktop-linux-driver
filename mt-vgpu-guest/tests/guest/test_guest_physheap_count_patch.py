import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import struct
import unittest


ROOT = get_repo_root()
SPEC = importlib.util.spec_from_file_location(
    "patch_guest_physheap_count",
    ROOT / "scripts/patch-guest-physheap-count.py",
)
patcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patcher)


class GuestPhysHeapCountPatchTests(unittest.TestCase):
    def make_body(self):
        body = bytearray(b"\x90" * (patcher.TRAMPOLINE_OFFSET + patcher.TRAMPOLINE_SIZE))
        body[patcher.BASE_COUNT_OFFSET:patcher.BASE_COUNT_OFFSET + len(patcher.BASE_COUNT)] = \
            patcher.BASE_COUNT
        body[patcher.GUEST_BRANCH_OFFSET:
             patcher.GUEST_BRANCH_OFFSET + len(patcher.GUEST_BRANCH_OLD)] = \
            patcher.GUEST_BRANCH_OLD
        body[patcher.TRAMPOLINE_OFFSET:
             patcher.TRAMPOLINE_OFFSET + patcher.TRAMPOLINE_SIZE] = \
            patcher.TRAMPOLINE_OLD
        return bytes(body)

    def test_decrement_runs_only_after_the_guest_mode_branch(self):
        original = self.make_body()
        patched = patcher.patch_sysdevinit_body(original)
        branch = patched[patcher.GUEST_BRANCH_OFFSET:
                         patcher.GUEST_BRANCH_OFFSET + len(patcher.GUEST_BRANCH_NEW)]
        branch_target = (patcher.GUEST_BRANCH_OFFSET + len(branch) +
                         struct.unpack_from("<i", branch, 2)[0])
        self.assertEqual(branch_target, patcher.TRAMPOLINE_OFFSET)
        self.assertEqual(
            patched[patcher.TRAMPOLINE_OFFSET:
                    patcher.TRAMPOLINE_OFFSET + patcher.TRAMPOLINE_SIZE],
            patcher.TRAMPOLINE_NEW,
        )
        jump_offset = patcher.TRAMPOLINE_OFFSET + len(patcher.TRAMPOLINE_PREFIX)
        jump_target = (jump_offset + 5 +
                       struct.unpack_from("<i", patched, jump_offset + 1)[0])
        self.assertEqual(jump_target, patcher.GUEST_BLOCK_OFFSET)
        self.assertEqual(
            patched[patcher.BASE_COUNT_OFFSET:
                    patcher.BASE_COUNT_OFFSET + len(patcher.BASE_COUNT)],
            patcher.BASE_COUNT,
        )

    def test_rejects_changed_branch_or_non_nop_trampoline(self):
        original = bytearray(self.make_body())
        original[patcher.GUEST_BRANCH_OFFSET] ^= 1
        with self.assertRaisesRegex(ValueError, "Guest-mode branch changed"):
            patcher.patch_sysdevinit_body(bytes(original))

        original = bytearray(self.make_body())
        original[patcher.TRAMPOLINE_OFFSET] ^= 1
        with self.assertRaisesRegex(ValueError, "expected NOP"):
            patcher.patch_sysdevinit_body(bytes(original))

    def test_rejects_changed_shared_count_instruction(self):
        original = bytearray(self.make_body())
        original[patcher.BASE_COUNT_OFFSET + 3] ^= 1
        with self.assertRaisesRegex(ValueError, "shared SysDevInit base-count"):
            patcher.patch_sysdevinit_body(bytes(original))

    def test_rejects_second_application(self):
        patched = patcher.patch_sysdevinit_body(self.make_body())
        with self.assertRaisesRegex(ValueError, "Guest-mode branch changed"):
            patcher.patch_sysdevinit_body(patched)


if __name__ == "__main__":
    unittest.main()
