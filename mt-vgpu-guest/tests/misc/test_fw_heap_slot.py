import importlib.util
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import unittest

ROOT = get_repo_root()
spec = importlib.util.spec_from_file_location(
    "fw_heap_slot", ROOT / "scripts/verify-fw-heap-slot.py")
fw_heap_slot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw_heap_slot)


class FirmwareHeapSlotTests(unittest.TestCase):
    def setUp(self):
        self.device_info = json.loads((ROOT / "reports/device-info.json").read_text())
        self.upload = json.loads((ROOT / "reports/firmware-upload-validation.json").read_text())

    def test_qy1_osid4_candidate_matches_saved_windows_segment_and_upload(self):
        result = fw_heap_slot.reconcile(
            self.device_info, self.upload, osid_start=4, osid_count=5, mpc_id=0)
        self.assertEqual(result["mpc_card_base_bytes"], "0x3e800000")
        self.assertEqual(result["computed_heap_bar2_offset"], "0x3f000000")
        self.assertEqual(result["linux_fw_premap_device_address"], "0xe1c2000000")
        self.assertEqual(result["inferred_gpu_mem_card_base"], "0x7717ef000")
        self.assertEqual(result["linux_guest_fw_main_scardbase_candidate"], "0x771fef000")
        self.assertTrue(result["fw_main_scardbase_matches_captured_windows_segment"])
        self.assertEqual(result["osid_relative_delta_from_first_guest_bytes"], "0x0")
        self.assertTrue(result["host_and_premap_relative_osid_delta_match"])
        self.assertFalse(result["premap_to_bar2_translation_proven"])
        self.assertTrue(result["saved_upload_matches_slot"])
        self.assertFalse(result["pvz_provider_proven"])

    def test_explicit_host_base_must_match_captured_slot(self):
        with self.assertRaisesRegex(ValueError, "firmware segment"):
            fw_heap_slot.reconcile(
                self.device_info, self.upload, osid_start=4, osid_count=5,
                mpc_id=0, mpc_card_base=0x3e000000)

    def test_guest_osid_must_be_in_supplied_topology(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            fw_heap_slot.reconcile(
                self.device_info, self.upload, osid_start=1, osid_count=3,
                mpc_id=0)

    def test_linux_osid7_candidate_is_calculated_without_reusing_osid4_upload(self):
        result = fw_heap_slot.reconcile(
            self.device_info, None, osid_start=4, osid_count=5, mpc_id=0,
            mpc_card_base=0x3e800000, guest_osid=7)
        self.assertEqual(result["computed_heap_bar2_offset"], "0x40800000")
        self.assertEqual(result["linux_fw_premap_device_address"], "0xe1c3800000")
        self.assertEqual(result["inferred_gpu_mem_card_base"], "0x7717ef000")
        self.assertEqual(result["linux_guest_fw_main_scardbase_candidate"], "0x7737ef000")
        self.assertEqual(result["fw_main_scardbase_delta_from_windows_reference_bytes"], "0x1800000")
        self.assertTrue(result["fw_main_scardbase_delta_matches_host_bar2_slot_delta"])
        self.assertFalse(result["fw_main_scardbase_matches_captured_windows_segment"])
        self.assertEqual(result["osid_relative_delta_from_first_guest_bytes"], "0x1800000")
        self.assertEqual(result["host_bar2_slot_relative_osid_delta_bytes"], "0x1800000")
        self.assertEqual(result["linux_fw_premap_relative_osid_delta_bytes"], "0x1800000")
        self.assertTrue(result["host_and_premap_relative_osid_delta_match"])
        self.assertIsNone(result["saved_upload_matches_slot"])
        self.assertEqual(result["device_info_osid"], 4)


if __name__ == "__main__":
    unittest.main()
