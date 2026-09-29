#!/usr/bin/env python3
"""Verify the offline Guest build selects a staged S3000 VZ firmware image."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-audit-20260929"


def run(*argv: str) -> str:
    return subprocess.check_output(argv, text=True, errors="replace")


def symbol_disassembly(core: Path, symbol: str) -> str:
    return run("objdump", "-dr", "-Mintel", f"--disassemble={symbol}", str(core))


def verify(build: Path = DEFAULT_BUILD) -> dict:
    core = build / "objs/x86_64/mtgpu_core.o_binary"
    module = build / "mtgpu.ko"
    firmware = build / "stage/lib/firmware/mthreads/musa.fw.1.0.0.0.vz.win"
    for path in (core, module, firmware):
        if not path.is_file():
            raise ValueError(f"missing candidate artifact: {path}")

    config = (build / "inc/config_kernel.h").read_text()
    params = (build / "src/mtgpu/mtgpu_module_param.c").read_text()
    if not re.search(r"^#define\s+RGX_NUM_OS_SUPPORTED\s+15\s*$", config, re.M):
        raise ValueError("candidate source no longer enables the 15-OS VZ configuration")
    if not re.search(
            r"^#define\s+PVRSRV_APPHINT_DRIVERMODE\s+0x80000001U\s*$", config, re.M):
        raise ValueError("candidate is not built with the Guest-only PVR driver mode")
    if not re.search(r"bool\s+mtgpu_load_windows_firmware\s*=\s*true\s*;", params):
        raise ValueError("Windows firmware selector is no longer enabled by default")
    if not re.search(r"module_param\(mtgpu_load_windows_firmware,\s*bool,\s*0444\)", params):
        raise ValueError("firmware selector mutability changed; re-audit before relying on default")

    getter = symbol_disassembly(core, "mtgpu_vgpu_is_win_fw_mode")
    wrapper = symbol_disassembly(core, "mtgpu_is_win_fw_mode")
    load = symbol_disassembly(core, "RGXLoadAndGetFWData")
    if not re.search(r"R_X86_64_PC32\s+mtgpu_load_windows_firmware", getter):
        raise ValueError("Guest firmware-mode getter no longer reads the module parameter")
    if not re.search(r"R_X86_64_PLT32\s+mtgpu_vgpu_is_win_fw_mode", wrapper):
        raise ValueError("PVR mode wrapper no longer delegates to the Guest selector")
    if not re.search(r"R_X86_64_PLT32\s+mtgpu_is_win_fw_mode", load):
        raise ValueError("PVR firmware load path no longer consults the mode wrapper")

    strings = run("readelf", "-p", ".rodata.str1.1", str(core))
    offsets = {}
    for line in strings.splitlines():
        match = re.match(r"\s*\[\s*([0-9a-f]+)\]\s+(.*)$", line)
        if match:
            offsets[match.group(2)] = int(match.group(1), 16)
    for suffix in (".vz.win", ".vz.linux"):
        if suffix not in offsets:
            raise ValueError(f"core string table is missing firmware suffix {suffix}")
        if not re.search(rf"\.rodata\.str1\.1\+0x{offsets[suffix]:x}\b", load):
            raise ValueError(f"PVR firmware loader no longer selects suffix {suffix}")
    if not re.search(r"test\s+eax,eax\s*\n\s*[0-9a-f]+:.*je\s+", load):
        raise ValueError("firmware mode branch shape changed; re-audit selection direction")
    win_ref = load.find(f".rodata.str1.1+0x{offsets['.vz.win']:x}")
    linux_ref = load.find(f".rodata.str1.1+0x{offsets['.vz.linux']:x}")
    if win_ref < 0 or linux_ref < 0 or win_ref >= linux_ref:
        raise ValueError("expected true-mode Windows suffix before false-mode Linux suffix")

    module_strings = run("strings", "-a", str(module))
    expected_win = "firmware=mthreads/musa.fw.1.0.0.0.vz.win"
    expected_linux = "firmware=mthreads/musa.fw.1.0.0.0.vz.linux"
    if expected_win not in module_strings or expected_linux not in module_strings:
        raise ValueError("candidate module metadata does not declare both VZ firmware variants")

    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    return {
        "candidate_build": str(build),
        "guest_selector_reads_module_parameter": True,
        "pvr_firmware_loader_uses_selector": True,
        "default_selector": "windows",
        "default_requested_firmware": "musa.fw.1.0.0.0.vz.win",
        "linux_fallback_firmware": "musa.fw.1.0.0.0.vz.linux",
        "default_firmware_staged": True,
        "staged_firmware_sha256": digest,
        "hardware_accessed": False,
    }


def main() -> None:
    build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_BUILD
    try:
        print(json.dumps(verify(build), indent=2))
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        raise SystemExit(str(exc))


if __name__ == "__main__":
    main()
