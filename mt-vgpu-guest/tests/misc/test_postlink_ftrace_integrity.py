import importlib.util
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
import unittest

ROOT = get_repo_root()
SPEC = importlib.util.spec_from_file_location(
    "verify_postlink_ftrace_integrity",
    ROOT / "scripts/verify-postlink-ftrace-integrity.py",
)
audit_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit_module)


class PostlinkFtraceIntegrityTests(unittest.TestCase):
    def test_accepts_only_changes_inside_patch_windows(self):
        baseline = bytes.fromhex("00 01 02 03 04 05 06 07")
        candidate = bytes.fromhex("00 01 aa 03 04 05 06 bb")
        self.assertEqual(
            audit_module.changed_offsets(baseline, candidate, [(2, 3), (7, 8)]),
            [2, 7],
        )

    def test_rejects_change_outside_patch_windows(self):
        baseline = bytes.fromhex("00 01 02 03 04")
        candidate = bytes.fromhex("00 01 02 aa 04")
        with self.assertRaisesRegex(ValueError, "outside guarded patch sites"):
            audit_module.changed_offsets(baseline, candidate, [(1, 2)])

    def test_rejects_text_section_size_change(self):
        with self.assertRaisesRegex(ValueError, "size changed"):
            audit_module.changed_offsets(b"abc", b"abcd", [(0, 1)])

    def test_requires_ftrace_relocation_sections_to_match(self):
        baseline = {name: {"data": b"same"} for name in audit_module.FTRACE_SECTIONS}
        candidate = {name: {"data": b"same"} for name in audit_module.FTRACE_SECTIONS}
        audit_module.verify_sections_equal(baseline, candidate, audit_module.FTRACE_SECTIONS)
        candidate[".rela__mcount_loc"]["data"] = b"changed"
        with self.assertRaisesRegex(ValueError, "ftrace/relocation metadata changed"):
            audit_module.verify_sections_equal(baseline, candidate, audit_module.FTRACE_SECTIONS)


if __name__ == "__main__":
    unittest.main()
