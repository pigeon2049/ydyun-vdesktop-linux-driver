import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "guest_fw_selector", ROOT / "scripts/verify-guest-fw-selector.py")
guest_fw_selector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_fw_selector)


class GuestFirmwareSelectorTests(unittest.TestCase):
    def test_candidate_default_selects_staged_s3000_windows_firmware(self):
        result = guest_fw_selector.verify()
        self.assertEqual(result["default_selector"], "windows")
        self.assertEqual(
            result["default_requested_firmware"],
            "musa.fw.1.0.0.0.vz.win",
        )
        self.assertTrue(result["default_firmware_staged"])
        self.assertFalse(result["hardware_accessed"])


if __name__ == "__main__":
    unittest.main()
