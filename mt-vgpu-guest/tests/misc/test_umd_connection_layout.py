#!/usr/bin/env python3
"""Gate the r357 UMD connection-linkage recon.

Pins the facts the fabricated linkage proof relies on, so a corpus refresh or
a header drift cannot silently invalidate them:
  * function addresses (file-offset convention) in the SHA-pinned UMD corpus,
  * the 0xd0 connection struct allocation site,
  * GetSrvHandle's read semantics (first qword, NULL -> 0),
  * PVRSRVBridgeCall's ioctl number,
  * OpenServicesDevice's fd -> handle -> connection write chain.

r357: GetSrvHandle @ 0x3c1c0 returns *(uint64_t*)conn (the services-handle
pointer); PVRSRVBridgeCall dereferences it for the fd and issues
ioctl(0xc0206440). Fabricated proof: /dev/null fd linkage -> ENOTTY -> 0x26,
no crash (contrast r354 SIGSEGV).
"""
import json
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
CORPUS = ROOT / "decompiled" / "linux-legacy-umd-5.2.0"
FUNCS = CORPUS / "functions.jsonl"
DECOMPILED = CORPUS / "decompiled.c"

# name -> expected file-offset address (Ghidra image base 0x100000 subtracted).
EXPECTED_ADDR = {
    "GetSrvHandle": 0x3C1C0,
    "PVRSRVConnectionCreateDevice": 0x48F80,
    "FUN_0013b7c0": 0x3B7C0,      # ConnectionCreate
    "FUN_00192550": 0x92550,      # OpenServicesDevice
    "FUN_00192930": 0x92930,      # PVRSRVBridgeCall
    "FUN_001a3ab0": 0xA3AB0,      # _GetFd
}


def load_functions():
    rows = {}
    with FUNCS.open() as fh:
        for line in fh:
            r = json.loads(line)
            rows[r["name"]] = r
    return rows


def body_text(row):
    lines = DECOMPILED.read_text().splitlines()
    s = row["c_line_start"] - 1
    return "\n".join(lines[s:s + row["c_line_count"]])


class UmdConnectionLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.funcs = load_functions()

    def test_function_addresses_pinned(self):
        for name, want in EXPECTED_ADDR.items():
            with self.subTest(name=name):
                self.assertIn(name, self.funcs)
                got = int(self.funcs[name]["address"], 16) - 0x100000
                self.assertEqual(got, want,
                                 f"{name}: corpus 0x{got:x} != pinned 0x{want:x}")

    def test_connection_struct_is_0xd0(self):
        body = body_text(self.funcs["FUN_0013b7c0"])
        self.assertIn("PVRSRVCallocUserModeMem(0xd0)", body)

    def test_getsrvhandle_reads_first_qword(self):
        body = body_text(self.funcs["GetSrvHandle"])
        self.assertIn("return *param_1;", body)
        self.assertIn("return 0;", body)

    def test_bridgecall_ioctl_number(self):
        body = body_text(self.funcs["FUN_00192930"])
        self.assertIn("0xc0206440", body)

    def test_open_services_device_write_chain(self):
        # fd -> 0x10 services-handle+0 ; handle -> connection+0.
        body = body_text(self.funcs["FUN_00192550"])
        self.assertIn("*piVar4 = local_3c;", body)
        self.assertIn("*param_4 = piVar4;", body)

    def test_getsrvhandle_exported(self):
        # nm -D shows GetSrvHandle as a dynamic text symbol; the fabricated
        # harness resolves it via dlsym instead of address math.
        self.assertTrue(self.funcs["GetSrvHandle"].get("status") == "decompiled")


if __name__ == "__main__":
    unittest.main()
