#!/usr/bin/env python3
"""Verify the built Guest driver enables PCI Bus Master before PVR startup."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = re.compile(r"R_X86_64_PLT32\s+([A-Za-z0-9_.$]+)-0x4")
REQUIRED_ORDER = (
    "os_pci_enable_device",
    "os_pci_set_master",
    "mtgpu_device_common_init",
    "mtgpu_vz_init",
)


def parse_call_targets(disassembly: str) -> list[str]:
    return TARGETS.findall(disassembly)


def audit_call_order(targets: list[str]) -> dict:
    positions: dict[str, int] = {}
    cursor = -1
    for target in REQUIRED_ORDER:
        try:
            cursor = targets.index(target, cursor + 1)
        except ValueError as exc:
            raise ValueError(
                f"mtgpu_probe does not call {target} in the required Guest startup order"
            ) from exc
        positions[target] = cursor

    return {
        "required_call_order": list(REQUIRED_ORDER),
        "observed_call_positions": positions,
        "pci_bus_master_enabled_before_common_init": True,
        "pci_bus_master_enabled_before_vz_init": True,
        "hardware_accessed": False,
    }


def audit_module(module: Path) -> dict:
    if not module.is_file():
        raise ValueError(f"module does not exist: {module}")
    objdump = shutil.which("objdump")
    if not objdump:
        raise ValueError("objdump is required to inspect the built module")

    result = subprocess.run(
        [objdump, "-dr", "-M", "intel", "--disassemble=mtgpu_probe", str(module)],
        check=True,
        capture_output=True,
        text=True,
    )
    data = module.read_bytes()
    return {
        "module": str(module.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        **audit_call_order(parse_call_targets(result.stdout)),
        "scope": "linked mtgpu_probe relocations only; no hardware behavior is inferred",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    args = parser.parse_args()
    try:
        report = audit_module(args.module)
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    output = ROOT / "reports/guest-probe-pci-master-validation.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
