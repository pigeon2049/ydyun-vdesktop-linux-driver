#!/usr/bin/env python3
"""Compare the v2 GDPA helper with the bounded Windows translator oracle."""

from __future__ import annotations

import ctypes
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from reference_oracle import ReferenceOracle


ROOT = Path(__file__).resolve().parents[1]
WINDOWS_DRIVER = Path("/opt/MTT-driver-only/mtkm64.sys")
WINDOWS_SHA256 = "0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33"
WINDOWS_TRANSLATOR = 0x140027AB4
WINDOWS_TRANSLATOR_END = 0x140027BEC
OUTER = 0x240000
INFO = 0x600000
WINDOW_BASE = 0x800000000
WINDOW_SIZE = 0x43000000
FW_BASE = 0x771FEF000
INFO_MAGIC = 0xAA557491


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--info', type=Path, default=ROOT / 'reports/device-info.bin')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'reports/v2-gdpa-compat-oracle-validation.json')
    args = parser.parse_args()
    if hashlib.sha256(WINDOWS_DRIVER.read_bytes()).hexdigest() != WINDOWS_SHA256:
        raise SystemExit("Windows RM image hash differs from the audited reference")
    raw_page = args.info.read_bytes()
    if len(raw_page) != 4096:
        raise SystemExit("expected the saved 4 KiB version-2 information page")
    if (struct.unpack_from('<II', raw_page, 0) != (INFO_MAGIC, 2) or
            struct.unpack_from('<Q', raw_page, 0x20)[0] != WINDOW_SIZE):
        raise SystemExit('oracle address cases require a v2 0x43000000 window')

    with tempfile.TemporaryDirectory(prefix="mtgpu-v2-gdpa-") as temp:
        library = Path(temp) / "libmtgpu_v2_gdpa.so"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-DMTGPU_COMPAT_USERSPACE_TEST",
            str(ROOT / "patches/mtgpu_vgpu_info_compat.c"), "-o", str(library),
        ], check=True)
        helper = ctypes.CDLL(str(library)).mtgpu_guest_v2_gdpa_to_host_compat
        helper.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                           ctypes.c_uint64, ctypes.c_uint64]
        helper.restype = ctypes.c_uint64
        page = (ctypes.c_ubyte * len(raw_page)).from_buffer_copy(raw_page)

        oracle = ReferenceOracle()
        oracle.uc.mem_write(OUTER, bytes(0x2000))
        oracle.put64(OUTER + 0x400, WINDOW_BASE)
        oracle.put64(OUTER + 0x408, WINDOW_SIZE)
        oracle.put64(OUTER + 0x1CB0, INFO)
        oracle.uc.mem_write(INFO, raw_page)

        offsets = {
            0, 1, 0x1FFFFF, 0x200000, 0x200001, 0x51FFFFF, 0x5200000,
            0x5200001, 0x3EFFFFFF, 0x3F000000, 0x3F000001,
            0x42FFFFFE, 0x42FFFFFF, 0x43000000,
        }
        rng = random.Random(0x27AB4)
        offsets.update(rng.randrange(0x43000001) for _ in range(128))
        cases = []
        for gdpa_offset in sorted(offsets):
            gdpa = WINDOW_BASE + gdpa_offset
            expected = oracle.run(
                WINDOWS_TRANSLATOR, [OUTER, gdpa],
                [(WINDOWS_TRANSLATOR, WINDOWS_TRANSLATOR_END)],
            )
            actual = helper(page, WINDOW_BASE, 0x43000000, gdpa)
            if actual != expected:
                raise SystemExit(
                    f"Windows v2 oracle mismatch at GDPA {gdpa:#x} "
                    f"(BAR2 offset {gdpa_offset:#x}): "
                    f"expected {expected:#x}, got {actual:#x}"
                )
            cases.append({"gdpa": hex(gdpa), "gdpa_offset": hex(gdpa_offset),
                          "host_device_paddr": hex(actual)})

        out_of_window_inputs = (0, WINDOW_BASE - 1, FW_BASE,
                                WINDOW_BASE + WINDOW_SIZE,
                                WINDOW_BASE + WINDOW_SIZE + 1)
        for gdpa in out_of_window_inputs:
            expected = oracle.run(
                WINDOWS_TRANSLATOR, [OUTER, gdpa],
                [(WINDOWS_TRANSLATOR, WINDOWS_TRANSLATOR_END)],
            )
            actual = helper(page, WINDOW_BASE, WINDOW_SIZE, gdpa)
            if actual != expected:
                raise SystemExit(
                    f"Windows v2 oracle mismatch outside BAR2 window at GDPA {gdpa:#x}: "
                    f"expected {expected:#x}, got {actual:#x}"
                )

        # The Windows translator skips flags 0x8/0x10 when building its packed
        # BAR2 cursor. Execute the reference function on a minimal synthetic
        # page as well, so those flags remain covered if the captured page
        # changes later.
        synthetic = bytearray(4096)
        struct.pack_into("<II", synthetic, 0, INFO_MAGIC, 2)
        struct.pack_into("<Q", synthetic, 0x20, 0x4000)
        struct.pack_into("<I", synthetic, 0xC50, 4)
        for index, (base, size, flags) in enumerate((
            (0, 0x1000, 0x8), (0, 0x1000, 0x10),
            (0x200000000, 0x1000, 0x2), (0x300000000, 0x1000, 0x4),
        )):
            struct.pack_into("<QQQ", synthetic, 0x28 + index * 24,
                             base, size, flags)
        oracle.put64(OUTER + 0x408, 0x4000)
        oracle.uc.mem_write(INFO, bytes(synthetic))
        synthetic_page = (ctypes.c_ubyte * len(synthetic)).from_buffer_copy(synthetic)
        filtered_offsets = (0, 0xFFF, 0x1000, 0x1FFF, 0x2000)
        for gdpa_offset in filtered_offsets:
            gdpa = WINDOW_BASE + gdpa_offset
            expected = oracle.run(
                WINDOWS_TRANSLATOR, [OUTER, gdpa],
                [(WINDOWS_TRANSLATOR, WINDOWS_TRANSLATOR_END)],
            )
            actual = helper(synthetic_page, WINDOW_BASE, 0x4000, gdpa)
            if actual != expected:
                raise SystemExit(
                    f"Windows v2 oracle mismatch for flags 0x8/0x10 fixture at "
                    f"GDPA {gdpa:#x}: expected {expected:#x}, got {actual:#x}"
                )

    report = {
        "windows_driver": str(WINDOWS_DRIVER),
        "windows_driver_sha256": WINDOWS_SHA256,
        "executed_windows_function": [hex(WINDOWS_TRANSLATOR),
                                       hex(WINDOWS_TRANSLATOR_END)],
        "execution_scope": "FUN_140027ab4 only; no imported Windows helpers",
        "guest_page": str(args.info.resolve()),
        "guest_page_sha256": hashlib.sha256(raw_page).hexdigest(),
        "bar2_window_base": hex(WINDOW_BASE),
        "bar2_window_size": hex(WINDOW_SIZE),
        "input_coordinate": "absolute GDPA = BAR2 base + tested BAR2 offset",
        "compared_offsets": len(cases),
        "all_results_match": True,
        "cases": cases,
        "compared_out_of_window_inputs": [hex(value) for value in out_of_window_inputs],
        "all_out_of_window_results_match": True,
        "synthetic_flags_8_10_offsets": [hex(value) for value in filtered_offsets],
        "all_synthetic_flags_8_10_results_match": True,
        "hardware_accessed": False,
    }
    out = args.output
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
