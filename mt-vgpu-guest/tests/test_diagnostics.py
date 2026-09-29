import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


doctor = load("guest-doctor")
audit = load("audit-windows")


class DiagnosticsTests(unittest.TestCase):
    def test_qxl_is_not_mtt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            qxl = root / "0000:00:02.0"
            qxl.mkdir()
            (qxl / "vendor").write_text("0x1b36\n")
            self.assertEqual(doctor.pci_devices(root), [])

    def test_unbound_mtt_is_not_working_driver(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gpu = root / "0000:00:0e.0"
            gpu.mkdir()
            (gpu / "vendor").write_text("0x1ed5\n")
            (gpu / "device").write_text("0x0222\n")
            devices = doctor.pci_devices(root)
            self.assertIsNone(devices[0]["driver"])
            self.assertEqual(devices[0]["drm_nodes"], [])

    def test_drm_uses_real_vendor_not_render_number(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gpu = root / "pci" / "0000:00:02.0"
            gpu.mkdir(parents=True)
            (gpu / "vendor").write_text("0x1b36")
            node = root / "drm" / "renderD128"
            node.mkdir(parents=True)
            (node / "device").symlink_to(gpu)
            nodes = doctor.drm_nodes(root / "drm")
            self.assertEqual(nodes[0]["vendor"], "0x1b36")

    def test_loaded_module_state_decodes_gnu_build_id(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "mtgpu"
            notes = root / "notes"
            notes.mkdir(parents=True)
            build_id = bytes(range(20))
            note = struct.pack("<III", 4, len(build_id), 3) + b"GNU\0" + build_id
            (notes / ".note.gnu.build-id").write_bytes(note)
            for name, value in (("initstate", "coming"), ("refcnt", "1"),
                                ("taint", "OE"), ("coresize", "4096"),
                                ("initsize", "0")):
                (root / name).write_text(value)
            state = doctor.module_state(base=Path(temp))
            self.assertTrue(state["present"])
            self.assertEqual(state["initstate"], "coming")
            self.assertEqual(state["build_id"], build_id.hex())

    def test_module_state_reports_absent_module(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertEqual(doctor.module_state(base=Path(temp)), {"present": False})

    def test_module_build_id_rejects_malformed_note(self):
        with tempfile.TemporaryDirectory() as temp:
            note = Path(temp) / "note"
            note.write_bytes(struct.pack("<III", 4, 20, 3) + b"BAD\0" + bytes(20))
            self.assertIsNone(doctor.module_build_id(note))

    def test_truncated_pe(self):
        self.assertIsNone(audit.pe_metadata(b"MZ"))
        data = bytearray(64)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3c, 0xfffffff0)
        self.assertIsNone(audit.pe_metadata(data))

    def test_pe_embedded_version(self):
        data = bytearray(192)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3c, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 68, 0x8664)
        struct.pack_into("<6I", data, 128, 0xfeef04bd, 0x10000,
                         30 << 16, (2505 << 16) | 1, (1 << 16) | 14, (593 << 16) | 6196)
        result = audit.pe_metadata(bytes(data))
        self.assertEqual(result["versions"][0]["file_version"], "30.0.2505.1")
        self.assertEqual(result["versions"][0]["product_version"], "1.14.593.6196")


if __name__ == "__main__":
    unittest.main()
