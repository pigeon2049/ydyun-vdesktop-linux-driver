import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import unittest

ROOT = get_repo_root()
SPEC = importlib.util.spec_from_file_location(
    "verify_guest_probe_pci_master",
    ROOT / "scripts/verify-guest-probe-pci-master.py",
)
probe_audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe_audit)


class GuestProbePciMasterTests(unittest.TestCase):
    def test_requires_bus_master_before_common_and_vz_init(self):
        targets = [
            "os_pci_enable_device",
            "pci_resource_start",
            "os_pci_set_master",
            "mtgpu_device_common_init",
            "mtgpu_vz_init",
        ]
        result = probe_audit.audit_call_order(targets)
        self.assertTrue(result["pci_bus_master_enabled_before_common_init"])
        self.assertTrue(result["pci_bus_master_enabled_before_vz_init"])

    def test_rejects_missing_master_enable(self):
        targets = [
            "os_pci_enable_device",
            "mtgpu_device_common_init",
            "mtgpu_vz_init",
        ]
        with self.assertRaisesRegex(ValueError, "os_pci_set_master"):
            probe_audit.audit_call_order(targets)

    def test_rejects_master_enable_after_pvr_start(self):
        targets = [
            "os_pci_enable_device",
            "mtgpu_device_common_init",
            "os_pci_set_master",
            "mtgpu_vz_init",
        ]
        with self.assertRaisesRegex(ValueError, "required Guest startup order"):
            probe_audit.audit_call_order(targets)

    def test_requires_vz_init_after_master_enable(self):
        targets = ["os_pci_enable_device", "os_pci_set_master", "mtgpu_device_common_init"]
        with self.assertRaisesRegex(ValueError, "mtgpu_vz_init"):
            probe_audit.audit_call_order(targets)


if __name__ == "__main__":
    unittest.main()
