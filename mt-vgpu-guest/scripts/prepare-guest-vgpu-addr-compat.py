#!/usr/bin/env python3
"""Prepare a private Linux Guest build copy for v1/v2 GDPA translation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


OLD = "GuestDevicePAddrToHostDevicePAddr"
V1 = "mtgpu_guest_v1_device_paddr_to_host_device_paddr"
WRAPPER = "src/mtgpu/vgpu/mtgpu_vgpu_addr_compat.c"
EXPECTED_ADDRESS = 0xE9D20
EXPECTED_SIZE = 0x12C


def run(args: list[str]) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def symbols(path: Path) -> dict[str, tuple[int, int, str]]:
    output = run(["nm", "-S", "--defined-only", str(path)])
    result = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S)\s+(.+)$", line)
        if match:
            result[match.group(4)] = (int(match.group(1), 16),
                                      int(match.group(2), 16), match.group(3))
    return result


def section_hash(path: Path, section: str, directory: Path) -> str:
    target = directory / (section.replace(".", "_") + ".bin")
    subprocess.run(["objcopy", f"--dump-section", f"{section}={target}", str(path)],
                   check=True, capture_output=True, text=True)
    return hashlib.sha256(target.read_bytes()).hexdigest()


def reloc_count(path: Path, symbol: str) -> int:
    output = run(["objdump", "-r", str(path)])
    return sum(symbol in line for line in output.splitlines())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("wrapper_source", type=Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    core = build_dir / "objs/x86_64/mtgpu_core.o_binary"
    makefile = build_dir / "Makefile"
    wrapper_dest = build_dir / WRAPPER
    if not core.is_file() or not makefile.is_file() or not args.wrapper_source.is_file():
        parser.error("build directory is missing its core object, Makefile, or wrapper source")
    if shutil.which("objcopy") is None or shutil.which("nm") is None:
        parser.error("binutils objcopy and nm are required")

    before = symbols(core)
    if OLD not in before or before[OLD][:2] != (EXPECTED_ADDRESS, EXPECTED_SIZE):
        parser.error("Guest core translator symbol does not match the audited 2.3.0 build")
    if V1 in before:
        parser.error("preserved v1 translator symbol is already present")
    make_text = makefile.read_text()
    if WRAPPER in make_text:
        parser.error("address-compat wrapper is already enabled in this build copy")

    with tempfile.TemporaryDirectory(prefix=".mtgpu-vgpu-addr-prep-",
                                     dir=core.parent) as temp:
        temp_dir = Path(temp)
        old_text_hash = section_hash(core, ".text", temp_dir)
        old_relocs = reloc_count(core, OLD)
        if old_relocs < 5:
            parser.error(f"expected active core call sites, found only {old_relocs}")
        rewritten = temp_dir / "mtgpu_core.o_binary"
        subprocess.run(["objcopy", "--redefine-sym", f"{OLD}={V1}",
                        str(core), str(rewritten)], check=True,
                        capture_output=True, text=True)
        after = symbols(rewritten)
        if OLD in after or V1 not in after or after[V1][:2] != before[OLD][:2]:
            parser.error("objcopy did not preserve the audited translator under its v1 name")
        new_text_hash = section_hash(rewritten, ".text", temp_dir)
        new_relocs = reloc_count(rewritten, V1)
        if old_text_hash != new_text_hash or old_relocs != new_relocs:
            parser.error("symbol rewrite changed core instructions or lost translator callers")
        os.replace(rewritten, core)

    wrapper_dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.wrapper_source, wrapper_dest)
    with makefile.open("a") as stream:
        stream.write(f"\nmtgpu-objs += {WRAPPER[:-2]}.o\n")
    report = {
        "core_object": str(core),
        "renamed_symbol": {"from": OLD, "to": V1,
                           "address": hex(before[OLD][0]), "size": hex(before[OLD][1])},
        "active_core_relocations_preserved": new_relocs,
        "core_text_sha256_before": old_text_hash,
        "core_text_sha256_after": new_text_hash,
        "wrapper_source": str(wrapper_dest),
        "hardware_touched": False,
    }
    report_path = build_dir / "guest-vgpu-addr-compat-preparation.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
