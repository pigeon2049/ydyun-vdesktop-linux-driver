import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import unittest

ROOT = get_repo_root()
SPEC = importlib.util.spec_from_file_location(
    "pvz_call_path", ROOT / "scripts/verify-pvz-call-path.py")
pvz_call_path = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pvz_call_path)


class PvzCallPathTests(unittest.TestCase):
    def setUp(self):
        self.asm = """\
00000000000ba460 <RGXInitCreateFWKernelMemoryContext>:
 ba5f1: call ba5f5
 ba5f1: R_X86_64_PLT32 RGXFwRawHeapAllocMap-0x4
 ba7b1: call ba7b6
 ba7b1: R_X86_64_PLT32 RGXFwRawHeapUnmapFree-0x4
00000000000ba820 <RGXDeInitDestroyFWKernelMemoryContext>:
 ba843: 75 2b jne ba870 <RGXDeInitDestroyFWKernelMemoryContext+0x50>
 ba862: call ba867
 ba862: R_X86_64_PLT32 RGXFwRawHeapUnmapFree-0x4
00000000000ba8a0 <next_function>:
"""

    def test_raw_heap_map_and_cleanup_calls_have_expected_function_sites(self):
        result = pvz_call_path.verify_reference_sweep(self.asm, [])
        self.assertEqual(result["raw_fw_heap_map_call_sites"], ["0xba5f1"])
        self.assertEqual(result["raw_fw_heap_unmap_call_sites"],
                         ["0xba7b1", "0xba862"])
        self.assertTrue(result["raw_heap_map_call_confined_to_fw_context_setup"])
        self.assertTrue(result["raw_heap_unmaps_confined_to_setup_failure_and_context_deinit"])

    def test_rejects_static_pvz_map_callback_call(self):
        changed = self.asm.replace(
            "00000000000ba8a0 <next_function>:",
            "ba900: call ba905\nba900: R_X86_64_PLT32 PvzClientMapDevPhysHeap-0x4\n"
            "00000000000ba8a0 <next_function>:")
        with self.assertRaisesRegex(ValueError, "PVZ client wrapper has static relocation"):
            pvz_call_path.verify_reference_sweep(changed, [])

    def test_rejects_wrapper_reference_in_official_source(self):
        with self.assertRaisesRegex(ValueError, "official Linux source has PVZ client"):
            pvz_call_path.verify_reference_sweep(self.asm, ["some_file.c:PvzClientMapDevPhysHeap"])


if __name__ == "__main__":
    unittest.main()
