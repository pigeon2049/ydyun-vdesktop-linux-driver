import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import unittest

ROOT = get_repo_root()
spec = importlib.util.spec_from_file_location(
    "static_guest_fw_premap", ROOT / "scripts/verify-static-guest-fw-premap.py")
static_guest_fw_premap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(static_guest_fw_premap)


class StaticGuestFirmwarePremapTests(unittest.TestCase):
    def test_saved_core_guest_branch_skips_host_raw_heap_map(self):
        body = (ROOT / "reports/host-2.3-core.asm").read_text()
        result = static_guest_fw_premap.verify(body)
        self.assertEqual(result["guest_driver_mode_value"], 1)
        self.assertTrue(result["guest_branch_bypasses_raw_heap_map"])
        self.assertTrue(result["guest_branch_sets_premap_status_twice"])
        self.assertEqual(len(result["premap_status_calls"]), 2)
        self.assertEqual(result["pvr_premap_osid_stride_bytes"], "0x800000")
        self.assertEqual(result["pvr_premap_blueprint_count"], 15)
        self.assertEqual(result["fw_premap7_heap_id"], 18)
        self.assertEqual(result["guest_osid7_rawheap_index"], 7)
        self.assertEqual(result["guest_osid7_premap_blueprint_address"], "0xe1c3800000")
        self.assertTrue(result["guest_osid7_selects_corresponding_rawheap"])
        self.assertTrue(result["premapped_va_preserves_offset_within_physheap"])
        self.assertFalse(result["premapped_va_helper_proves_host_bar2_mapping"])
        self.assertTrue(result["host_raw_heap_map_not_called_by_guest_branch"])
        self.assertIn("not established", result["pvz_client_callback_usage"])

    def test_rejects_a_core_that_moves_mapping_into_guest_path(self):
        body = (ROOT / "reports/host-2.3-core.asm").read_text()
        changed = body.replace("ba729 <RGXInitCreateFWKernelMemoryContext+0x2c9>",
                               "ba5e5 <RGXInitCreateFWKernelMemoryContext+0x2c9>",
                               1)
        with self.assertRaisesRegex(ValueError, "bypass"):
            static_guest_fw_premap.verify(changed)

    def test_rejects_guest_osid7_outside_configured_premap_range(self):
        body = (ROOT / "reports/host-2.3-core.asm").read_text()
        config = static_guest_fw_premap.CONFIG_HEADER.read_text()
        changed = config.replace("#define RGX_NUM_OS_SUPPORTED 15",
                                 "#define RGX_NUM_OS_SUPPORTED 7", 1)
        with self.assertRaisesRegex(ValueError, "FW_PREMAP7"):
            static_guest_fw_premap.verify(body, config_header=changed)

    def test_rejects_wrong_fw_premap7_heap_id(self):
        body = (ROOT / "reports/host-2.3-core.asm").read_text()
        heap_ids = static_guest_fw_premap.HEAP_ID_HEADER.read_text()
        changed = heap_ids.replace("PVRSRV_PHYS_HEAP_FW_PREMAP7   18U",
                                   "PVRSRV_PHYS_HEAP_FW_PREMAP7   17U", 1)
        with self.assertRaisesRegex(ValueError, "FW_PREMAP7"):
            static_guest_fw_premap.verify(body, heap_id_header=changed)


if __name__ == "__main__":
    unittest.main()
