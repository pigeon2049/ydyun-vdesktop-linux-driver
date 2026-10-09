import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "verify_vgpu_package_guest_os", ROOT / "scripts/verify-vgpu-package-guest-os.py")
package_audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_audit)


class VgpuPackageGuestAuditTests(unittest.TestCase):
    def setUp(self):
        self.names = [
            package_audit.GUEST_PREFIX + name
            for name in package_audit.EXPECTED_WINDOWS_GUEST_DRIVERS
        ] + [
            package_audit.GUEST_PREFIX + "mtvpu-01-1.0.bin",
            package_audit.DKMS_PREFIX + "mtgpu-1.0.0.amd64.deb",
            package_audit.DKMS_PREFIX + "mtgpu-1.0.0.amd64.rpm",
        ]

    def test_separates_windows_guest_payload_from_linux_host_dkms(self):
        result = package_audit.audit_member_names(self.names)
        self.assertFalse(result["linux_guest_driver_like_payloads"])
        self.assertFalse(result["linux_guest_driver_package_present_in_this_archive"])
        self.assertEqual(len(result["windows_guest_driver_files"]), 3)
        self.assertEqual(len(result["linux_host_dkms_packages"]), 2)
        self.assertFalse(result["executed_or_installed_payloads"])

    def test_rejects_linux_driver_payload_misplaced_in_guest_directory(self):
        changed = self.names + [package_audit.GUEST_PREFIX + "mtgpu.ko"]
        with self.assertRaisesRegex(ValueError, "under Guest directory"):
            package_audit.audit_member_names(changed)

    def test_requires_the_guest_windows_driver_files(self):
        changed = [name for name in self.names if not name.endswith("mtkm64.sys")]
        with self.assertRaisesRegex(ValueError, "Windows Guest driver files are missing"):
            package_audit.audit_member_names(changed)

    def test_requires_linux_host_dkms_package(self):
        changed = [name for name in self.names if not name.endswith((".deb", ".rpm"))]
        with self.assertRaisesRegex(ValueError, "Linux DKMS Host package is absent"):
            package_audit.audit_member_names(changed)


if __name__ == "__main__":
    unittest.main()
