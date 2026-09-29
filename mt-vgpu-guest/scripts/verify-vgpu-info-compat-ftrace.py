#!/usr/bin/env python3
"""Check dual-version info-page post-link edits preserve the linked module."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
_integrity_path = Path(__file__).with_name("verify-postlink-ftrace-integrity.py")
_spec = importlib.util.spec_from_file_location("postlink_integrity", _integrity_path)
_integrity = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
_spec.loader.exec_module(_integrity)

FW_FUNCTION = "mtgpu_platform_data_vz_init"
FW_OFFSET = 0x8D
FW_OLD = bytes.fromhex("49 8b 84 24 e0 10 00 00")
SIZE_FUNCTION = "mtgpu_device_memory_fixup"
SIZE_OFFSET = 0x447
SIZE_OLD = bytes.fromhex("48 8b 8a 38 04 00 00")
SIZE_NEW = bytes.fromhex("48 8b 4a 20 90 90 90")
SHARE_OFFSET = 0x390
SHARE_SIZE_OFFSET = 0x3A3
SHARE_OLD = bytes.fromhex(
    "49 8b 94 24 38 11 00 00 "
    "48 8b 92 48 04 00 00 "
    "48 89 50 10"
)
SHARE_SIZE_OLD = bytes.fromhex("48 c7 40 20 00 80 00 00")
SHARE_SIZE_NEW = b"\x90" * len(SHARE_SIZE_OLD)
SHARE_CALL_PREFIX = bytes.fromhex("48 89 c7 4c 89 e6 e8")
SHARE_HELPER = "mtgpu_guest_vpu_share_mem_addr_compat"
INFO_ALLOCATION_OFFSET = 0x24A
INFO_ALLOCATION_OLD = bytes.fromhex("48 8d bb 9f 08 00 00")
INFO_ALLOCATION_NEW = bytes.fromhex("48 8d bb ff 0f 00 00")
FW_SIZE_READS = (
    (0x2AD, bytes.fromhex("8b 82 50 08 00 00"), b"\xb8" + struct.pack("<I", 0x800000) + b"\x90"),
    (0x2CD, bytes.fromhex("8b 92 50 08 00 00"), b"\xba" + struct.pack("<I", 0x800000) + b"\x90"),
    (0x312, bytes.fromhex("8b 90 50 08 00 00"), b"\xba" + struct.pack("<I", 0x800000) + b"\x90"),
)
HEAP_FUNCTION = "SysDevInit"
FTRACE_SECTIONS = (
    "__mcount_loc",
    ".rela__mcount_loc",
    "__patchable_function_entries",
    ".rela__patchable_function_entries",
)


def section_location(sections, symbols, symbol: str, offset: int) -> tuple[int, int]:
    value = _integrity.unique_symbol(symbols, symbol)
    text = sections[".text"]
    if value < text["address"]:
        raise ValueError(f"symbol lies outside .text: {symbol}")
    delta = value - text["address"] + offset
    if delta < 0 or delta > text["size"]:
        raise ValueError(f"instruction offset lies outside .text: {symbol}+{offset:#x}")
    return text["offset"] + delta, delta


def audit(baseline_path: Path, candidate_path: Path) -> dict:
    old_data, old_sections, old_symbols = _integrity.parse_elf(baseline_path)
    new_data, new_sections, new_symbols = _integrity.parse_elf(candidate_path)
    if len(old_data) != len(new_data):
        raise ValueError("post-link edits changed module file length")
    old_text, new_text = old_sections[".text"], new_sections[".text"]
    for field in ("address", "offset", "size"):
        if old_text[field] != new_text[field]:
            raise ValueError(f".text {field} changed during post-link edits")
    _integrity.verify_sections_equal(old_sections, new_sections, FTRACE_SECTIONS)

    helper = _integrity.unique_symbol(old_symbols, "mtgpu_guest_fw_heap_base_compat")
    if helper != _integrity.unique_symbol(new_symbols, "mtgpu_guest_fw_heap_base_compat"):
        raise ValueError("info-page helper moved during post-link patching")
    share_helper = _integrity.unique_symbol(old_symbols, SHARE_HELPER)
    if share_helper != _integrity.unique_symbol(new_symbols, SHARE_HELPER):
        raise ValueError("VPU shared-memory helper moved during post-link patching")

    fw_file, fw_delta = section_location(old_sections, old_symbols, FW_FUNCTION, FW_OFFSET)
    fw_value = _integrity.unique_symbol(old_symbols, FW_FUNCTION) + FW_OFFSET
    displacement = helper - (fw_value + len(FW_OLD))
    if displacement < -(1 << 31) or displacement >= (1 << 31):
        raise ValueError("info-page helper is outside rel32 call range")
    fw_new = bytes.fromhex("48 89 c7 e8") + struct.pack("<i", displacement)
    size_file, size_delta = section_location(old_sections, old_symbols, SIZE_FUNCTION, SIZE_OFFSET)
    share_file, share_delta = section_location(old_sections, old_symbols, SIZE_FUNCTION, SHARE_OFFSET)
    share_size_file, share_size_delta = section_location(
        old_sections, old_symbols, SIZE_FUNCTION, SHARE_SIZE_OFFSET)
    allocation_file, allocation_delta = section_location(
        old_sections, old_symbols, SIZE_FUNCTION, INFO_ALLOCATION_OFFSET)
    share_value = _integrity.unique_symbol(old_symbols, SIZE_FUNCTION) + SHARE_OFFSET
    share_displacement = share_helper - (share_value + len(SHARE_CALL_PREFIX) + 4)
    if share_displacement < -(1 << 31) or share_displacement >= (1 << 31):
        raise ValueError("VPU shared-memory helper is outside rel32 call range")
    share_new = (SHARE_CALL_PREFIX + struct.pack("<i", share_displacement) +
                 b"\x90" * (len(SHARE_OLD) - len(SHARE_CALL_PREFIX) - 4))
    patches = (
        (FW_FUNCTION, FW_OFFSET, fw_file, fw_delta, FW_OLD, fw_new),
        (SIZE_FUNCTION, SIZE_OFFSET, size_file, size_delta, SIZE_OLD, SIZE_NEW),
        (SIZE_FUNCTION, SHARE_OFFSET, share_file, share_delta, SHARE_OLD, share_new),
        (SIZE_FUNCTION, SHARE_SIZE_OFFSET, share_size_file, share_size_delta,
         SHARE_SIZE_OLD, SHARE_SIZE_NEW),
        (SIZE_FUNCTION, INFO_ALLOCATION_OFFSET, allocation_file, allocation_delta,
         INFO_ALLOCATION_OLD, INFO_ALLOCATION_NEW),
    ) + tuple(
        (patch["symbol"], patch["offset"],
         *section_location(old_sections, old_symbols, patch["symbol"], patch["offset"]),
         patch["old"], patch["new"])
        for patch in _integrity.PATCHES if patch["symbol"] == HEAP_FUNCTION
    ) + tuple(
        (SIZE_FUNCTION, offset, *section_location(old_sections, old_symbols, SIZE_FUNCTION, offset), old, new)
        for offset, old, new in FW_SIZE_READS
    )
    allowed = []
    patch_results = []
    for name, offset, file_offset, text_offset, expected_old, expected_new in patches:
        old_value = _integrity.unique_symbol(old_symbols, name)
        if old_value != _integrity.unique_symbol(new_symbols, name):
            raise ValueError(f"symbol moved during post-link patching: {name}")
        if old_data[file_offset:file_offset + len(expected_old)] != expected_old:
            raise ValueError(f"unexpected baseline bytes at {name}+{offset:#x}")
        if new_data[file_offset:file_offset + len(expected_new)] != expected_new:
            raise ValueError(f"unexpected candidate bytes at {name}+{offset:#x}")
        allowed.append((file_offset, file_offset + len(expected_new)))
        patch_results.append({
            "symbol": name,
            "symbol_offset": hex(offset),
            "module_file_offset": hex(file_offset),
            "old_bytes": expected_old.hex(),
            "new_bytes": expected_new.hex(),
        })

    unchanged_guards = []
    for guard in _integrity.UNCHANGED_GUARDS:
        name = guard["symbol"]
        offset = guard["offset"]
        old_file, _old_delta = section_location(old_sections, old_symbols, name, offset)
        new_file, _new_delta = section_location(new_sections, new_symbols, name, offset)
        if (old_data[old_file:old_file + len(guard["bytes"])] != guard["bytes"] or
                new_data[new_file:new_file + len(guard["bytes"])] != guard["bytes"]):
            raise ValueError(f"unchanged instruction guard failed at {name}+{offset:#x}")
        unchanged_guards.append({
            "symbol": name,
            "symbol_offset": hex(offset),
            "bytes": guard["bytes"].hex(),
            "description": guard["description"],
        })

    changed = [i for i, (a, b) in enumerate(zip(old_data, new_data)) if a != b]
    unexpected = [i for i in changed if not any(lo <= i < hi for lo, hi in allowed)]
    if unexpected:
        raise ValueError("unexpected module bytes changed outside approved windows: " +
                         ", ".join(hex(i) for i in unexpected[:8]))
    expected_changed = sorted({
        start + i
        for (_, _, start, _text, old, new) in patches
        for i, (a, b) in enumerate(zip(old, new)) if a != b
    })
    if changed != expected_changed:
        raise ValueError("candidate does not contain exactly the expected changed bytes")
    return {
        "baseline": str(baseline_path.resolve()),
        "baseline_sha256": hashlib.sha256(old_data).hexdigest(),
        "candidate": str(candidate_path.resolve()),
        "candidate_sha256": hashlib.sha256(new_data).hexdigest(),
        "patches": patch_results,
        "unchanged_guards": unchanged_guards,
        "changed_byte_count": len(changed),
        "ftrace_metadata_sections_unchanged": list(FTRACE_SECTIONS),
        "only_expected_postlink_bytes_changed": True,
        "hardware_accessed": False,
        "scope": "same-build ELF and ftrace integrity only; no module-load claim",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="module immediately after Kbuild link")
    parser.add_argument("candidate", type=Path, help="module after offline byte patches")
    args = parser.parse_args()
    try:
        result = audit(args.baseline, args.candidate)
    except (OSError, ValueError, struct.error) as exc:
        raise SystemExit(str(exc)) from exc
    output = ROOT / "reports/vgpu-info-compat-ftrace-validation.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
