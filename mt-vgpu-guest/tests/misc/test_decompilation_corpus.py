import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import corpus


class CorpusInventoryTests(unittest.TestCase):
    def test_complete_manifest_covers_sys_and_dll_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            (source / "kernel.sys").write_bytes(b"sys")
            (source / "user.dll").write_bytes(b"dll")
            (source / "notes.inf").write_bytes(b"not part of corpus")
            manifest = {"input": str(source), "file_count": 2,
                        "files": {"kernel.sys": {}, "user.dll": {}}}

            self.assertEqual(corpus.inventory_problems(manifest), [])

    def test_partial_manifest_reports_omitted_binary(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            (source / "kernel.sys").write_bytes(b"sys")
            (source / "user.dll").write_bytes(b"dll")
            manifest = {"input": str(source), "file_count": 1,
                        "files": {"kernel.sys": {}}}

            self.assertIn("Missing binaries: user.dll", corpus.inventory_problems(manifest))

    def test_manifest_rejects_stale_binary_entry(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            (source / "kernel.sys").write_bytes(b"sys")
            manifest = {"input": str(source), "file_count": 1,
                        "files": {"old.dll": {}}}

            problems = corpus.inventory_problems(manifest)
            self.assertIn("Missing binaries: kernel.sys", problems)
            self.assertIn("Unexpected binaries: old.dll", problems)


if __name__ == "__main__":
    unittest.main()
