#!/usr/bin/env python3
"""Verify the Guest-only SysDevInit heap-count fix and its known failure site."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
PATCHER_PATH = ROOT / "scripts/patch-guest-physheap-count.py"
SPEC = importlib.util.spec_from_file_location("guest_heap_count_patcher", PATCHER_PATH)
PATCHER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PATCHER)

GUEST_BRANCH = bytes.fromhex("0f 85 55 06 00 00")
TRAMPOLINE = bytes.fromhex("41 83 ed 01 e9 51 fa ff ff 90")
SHARED_COUNT = bytes.fromhex("41 83 c5 06")
HEAP_COUNT_COMPARE = bytes.fromhex("44 3b 6d b0")
HEAP_COUNT_FAILURE_BRANCH = bytes.fromhex("0f 85 51 03 00 00")
HEAP_COUNT_ERROR = bytes.fromhex("ba 84 02 00 00")


def require(actual: bytes, expected: bytes, where: str) -> None:
    if actual != expected:
        raise ValueError(f"unexpected {where}: {actual.hex()} (expected {expected.hex()})")


def inspect_module(path: Path) -> dict:
    data = path.read_bytes()
    if PATCHER.BUILD_TAG not in data:
        raise ValueError("module does not contain the audited 5c6c275 core build tag")
    sections, named = PATCHER.section_table(data)
    symbol_value, text_addr = PATCHER.find_sysdevinit(data, sections, named)
    text = named[".text"][1]
    if symbol_value < text_addr:
        raise ValueError("SysDevInit symbol is outside .text")
    body_start = text[4] + symbol_value - text_addr

    def body(offset: int, size: int) -> bytes:
        value = data[body_start + offset:body_start + offset + size]
        if len(value) != size:
            raise ValueError(f"truncated SysDevInit at +{offset:#x}")
        return value

    require(body(PATCHER.BASE_COUNT_OFFSET, len(SHARED_COUNT)), SHARED_COUNT,
            "shared/non-Guest heap-count instruction")
    require(body(PATCHER.GUEST_BRANCH_OFFSET, len(GUEST_BRANCH)), GUEST_BRANCH,
            "Guest-mode branch")
    require(body(PATCHER.TRAMPOLINE_OFFSET, len(TRAMPOLINE)), TRAMPOLINE,
            "Guest-only count trampoline")
    require(body(0x434, len(HEAP_COUNT_COMPARE)), HEAP_COUNT_COMPARE,
            "descriptor-count comparison")
    require(body(0x438, len(HEAP_COUNT_FAILURE_BRANCH)), HEAP_COUNT_FAILURE_BRANCH,
            "heap-count mismatch branch")
    require(body(0x78F, len(HEAP_COUNT_ERROR)), HEAP_COUNT_ERROR,
            "heap-count mismatch error code")

    branch_target = PATCHER.GUEST_BRANCH_OFFSET + len(GUEST_BRANCH) + struct.unpack_from(
        "<i", GUEST_BRANCH, 2)[0]
    jump_offset = PATCHER.TRAMPOLINE_OFFSET + len(PATCHER.TRAMPOLINE_PREFIX)
    trampoline_target = jump_offset + 5 + struct.unpack_from("<i", TRAMPOLINE, len(PATCHER.TRAMPOLINE_PREFIX) + 1)[0]
    failure_target = 0x438 + len(HEAP_COUNT_FAILURE_BRANCH) + struct.unpack_from(
        "<i", HEAP_COUNT_FAILURE_BRANCH, 2)[0]
    if branch_target != PATCHER.TRAMPOLINE_OFFSET:
        raise ValueError("Guest branch does not reach the count trampoline")
    if trampoline_target != PATCHER.GUEST_BLOCK_OFFSET:
        raise ValueError("count trampoline does not rejoin the original Guest block")
    if failure_target != 0x78F:
        raise ValueError("count mismatch branch no longer reaches the audited error site")

    return {
        "module": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        "core_build_tag": PATCHER.BUILD_TAG.decode("ascii"),
        "guest_branch_targets_trampoline": True,
        "trampoline_subtracts_one_only_on_guest_branch": True,
        "shared_non_guest_count_unchanged": True,
        "count_compare_offset": "0x434",
        "mismatch_error_site_offset": "0x78f",
        "mismatch_error_decimal": 644,
        "mismatch_error_hex": "0x284",
        "known_runtime_case": {
            "driver_mode": "Guest",
            "mem_mode": 1,
            "reported_error": 644,
            "static_generated_heap_count": 4,
            "unpatched_expected_count": 5,
            "candidate_expected_count": 4,
            "source": "reports/official-linux-guest-port.md recorded run",
        },
        "candidate_matches_recorded_local_guest_count_failure": True,
        "scope": "linked-byte and control-flow verification; no module load or device access",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, help="linked mtgpu.ko to inspect")
    args = parser.parse_args()
    try:
        report = inspect_module(args.module)
    except (OSError, ValueError, SystemExit) as exc:
        raise SystemExit(f"Guest heap-count candidate check failed: {exc}")
    out = args.module.parent / "guest-physheap-count-candidate-validation.json"
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
