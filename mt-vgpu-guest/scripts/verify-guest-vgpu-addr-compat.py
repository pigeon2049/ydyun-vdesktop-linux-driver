#!/usr/bin/env python3
"""Verify the linked module contains both versioned GDPA translator paths."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


V1 = "mtgpu_guest_v1_device_paddr_to_host_device_paddr"
PUBLIC = "GuestDevicePAddrToHostDevicePAddr"
V2 = "mtgpu_guest_v2_gdpa_to_host_compat"


def output(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def read_symbols(path: Path) -> dict[str, tuple[int, int, str]]:
    result = {}
    for line in output("nm", "-S", "--defined-only", str(path)).splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S)\s+(.+)$", line)
        if match:
            result[match.group(4)] = (int(match.group(1), 16),
                                      int(match.group(2), 16), match.group(3))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    module = build_dir / "mtgpu.ko"
    core = build_dir / "objs/x86_64/mtgpu_core.o_binary"
    prep_path = build_dir / "guest-vgpu-addr-compat-preparation.json"
    if not module.is_file() or not core.is_file() or not prep_path.is_file():
        parser.error("missing linked module or address-compat preparation evidence")
    prep = json.loads(prep_path.read_text())
    symbols = read_symbols(module)
    core_symbols = read_symbols(core)
    for symbol in (PUBLIC, V1, V2):
        if symbol not in symbols:
            parser.error(f"linked module is missing {symbol}")
    if symbols[PUBLIC][0] == symbols[V1][0]:
        parser.error("public version dispatcher aliases the original v1 body")
    if V1 not in core_symbols or core_symbols[V1][0] != int(prep["renamed_symbol"]["address"], 16):
        parser.error("original v1 translator body did not retain its audited address")
    relocations = output("objdump", "-r", str(core))
    core_refs = sum(V1 in line for line in relocations.splitlines())
    if core_refs != prep["active_core_relocations_preserved"]:
        parser.error("one or more precompiled callers no longer target the v1 fallback")
    report = {
        "module": str(module),
        "public_dispatcher": hex(symbols[PUBLIC][0]),
        "preserved_v1_body": hex(symbols[V1][0]),
        "v2_segment_translator": hex(symbols[V2][0]),
        "precompiled_v1_callers": core_refs,
        "v1_body_address_preserved": True,
        "hardware_touched": False,
        "scope": "pre-postlink symbol/body check; final core call routing is separately audited in guest-vgpu-addr-callers-validation.json",
    }
    report_path = build_dir / "guest-vgpu-addr-compat-validation.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
