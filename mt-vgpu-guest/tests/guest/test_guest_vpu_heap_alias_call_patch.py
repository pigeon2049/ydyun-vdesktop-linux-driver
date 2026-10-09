import importlib.util
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root


SCRIPT = get_repo_root() / "scripts" / "patch-guest-vpu-heap-alias-call.py"
SPEC = importlib.util.spec_from_file_location("guest_vpu_heap_alias_call_patch", SCRIPT)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class GuestVpuHeapAliasCallPatchTest(unittest.TestCase):
	def test_redirects_only_the_relocation_symbol_index(self):
		original_symbol = 0x18FF
		wrapper_symbol = 0x1C42
		relocation_type = PATCH.R_X86_64_PLT32
		old_info = (original_symbol << 32) | relocation_type
		patched = PATCH.redirected_info(old_info, original_symbol, wrapper_symbol)
		self.assertEqual(patched >> 32, wrapper_symbol)
		self.assertEqual(patched & 0xFFFFFFFF, relocation_type)

	def test_refuses_a_changed_original_symbol(self):
		old_info = (0x18FF << 32) | PATCH.R_X86_64_PLT32
		with self.assertRaises(ValueError):
			PATCH.redirected_info(old_info, 0x1900, 0x2400)

	def test_refuses_a_non_plt32_relocation(self):
		old_info = (0x18FF << 32) | 2
		with self.assertRaises(ValueError):
			PATCH.redirected_info(old_info, 0x18FF, 0x2400)


if __name__ == "__main__":
	unittest.main()
