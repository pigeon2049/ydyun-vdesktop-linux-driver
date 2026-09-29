#!/usr/bin/env python3
"""Prove opt-in .ko byte patches preserve the build's ftrace tables."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

PATCHES = (
    {
        "symbol": "SysDevInit",
        "offset": 0x25B,
        "old": bytes.fromhex("0f 85 af 00 00 00"),
        "new": bytes.fromhex("0f 85 55 06 00 00"),
    },
    {
        "symbol": "SysDevInit",
        "offset": 0x8B6,
        "old": bytes.fromhex("66 2e 0f 1f 84 00 00 00 00 00"),
        "new": bytes.fromhex("41 83 ed 01 e9 51 fa ff ff 90"),
    },
)
UNCHANGED_GUARDS = (
    {
        "symbol": "SysDevInit",
        "offset": 0x92,
        "bytes": bytes.fromhex("41 83 c5 06"),
        "description": "shared base-count instruction remains unchanged",
    },
)
FTRACE_SECTIONS = (
    "__mcount_loc",
    ".rela__mcount_loc",
    "__patchable_function_entries",
    ".rela__patchable_function_entries",
)


def parse_elf(path: Path) -> tuple[bytes, dict[str, dict], dict[str, list[int]]]:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4:6] != b"\x02\x01":
        raise ValueError(f"not little-endian ELF64: {path}")
    if struct.unpack_from("<H", data, 18)[0] != 62:
        raise ValueError(f"not an x86-64 ELF module: {path}")

    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if shentsize < 64 or shstrndx >= shnum or shoff + shentsize * shnum > len(data):
        raise ValueError(f"invalid section table: {path}")

    raw = [
        struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        for i in range(shnum)
    ]
    shstr = raw[shstrndx]
    name_table = data[shstr[4] : shstr[4] + shstr[5]]
    sections: dict[str, dict] = {}
    for index, sh in enumerate(raw):
        end = name_table.find(b"\0", sh[0])
        if end < 0:
            raise ValueError(f"unterminated section name: {path}")
        name = name_table[sh[0] : end].decode("ascii", "replace")
        size = sh[5]
        content = b"" if sh[1] == 8 else data[sh[4] : sh[4] + size]
        if len(content) != (0 if sh[1] == 8 else size):
            raise ValueError(f"truncated section {name}: {path}")
        sections[name] = {
            "index": index,
            "type": sh[1],
            "address": sh[3],
            "offset": sh[4],
            "size": size,
            "data": content,
            "link": sh[6],
            "entsize": sh[9],
        }

    symbols: dict[str, list[int]] = {}
    text_index = sections.get(".text", {}).get("index")
    if text_index is None:
        raise ValueError(f"module has no .text section: {path}")
    for symsec in sections.values():
        if symsec["type"] not in (2, 11):
            continue
        if symsec["link"] >= len(raw) or symsec["entsize"] < 24:
            continue
        strings_sec = raw[symsec["link"]]
        strings = data[strings_sec[4] : strings_sec[4] + strings_sec[5]]
        start = symsec["offset"]
        stop = start + symsec["size"]
        for pos in range(start, stop, symsec["entsize"]):
            if pos + 24 > len(data):
                raise ValueError(f"truncated symbol table: {path}")
            nameoff, _info, _other, shndx, value, _size = struct.unpack_from("<IBBHQQ", data, pos)
            if nameoff >= len(strings) or shndx != text_index:
                continue
            end = strings.find(b"\0", nameoff)
            if end < 0:
                continue
            name = strings[nameoff:end].decode("ascii", "replace")
            symbols.setdefault(name, []).append(value)
    return data, sections, symbols


def unique_symbol(symbols: dict[str, list[int]], name: str) -> int:
    values = set(symbols.get(name, []))
    if not values:
        raise ValueError(f"missing .text symbol: {name}")
    if len(values) != 1:
        raise ValueError(f"ambiguous .text symbol: {name}")
    return values.pop()


def changed_offsets(baseline: bytes, candidate: bytes, allowed: list[tuple[int, int]]) -> list[int]:
    if len(baseline) != len(candidate):
        raise ValueError(".text section size changed")
    changed = [i for i, (a, b) in enumerate(zip(baseline, candidate)) if a != b]
    unexpected = [i for i in changed if not any(lo <= i < hi for lo, hi in allowed)]
    if unexpected:
        shown = ", ".join(hex(i) for i in unexpected[:8])
        raise ValueError(f"unexpected .text changes outside guarded patch sites: {shown}")
    return changed


def verify_sections_equal(baseline: dict[str, dict], candidate: dict[str, dict], names: tuple[str, ...]) -> None:
    for name in names:
        if name not in baseline or name not in candidate:
            raise ValueError(f"required metadata section is missing: {name}")
        if baseline[name]["data"] != candidate[name]["data"]:
            raise ValueError(f"ftrace/relocation metadata changed: {name}")


def audit(baseline_path: Path, candidate_path: Path) -> dict:
    # Keep one authoritative byte-level implementation for this build's six
    # info-page edits and Guest-only heap-count trampoline. This module remains
    # the shared ELF/ftrace parser used by that verifier.
    verifier_path = Path(__file__).with_name("verify-vgpu-info-compat-ftrace.py")
    spec = importlib.util.spec_from_file_location("vgpu_info_compat_verifier", verifier_path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load the current post-link verifier")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    report = verifier.audit(baseline_path, candidate_path)
    report["scope"] = "baseline-to-candidate ELF/ftrace integrity only; no runtime module-load claim"
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="known clean module before post-link patches")
    parser.add_argument("candidate", type=Path, help="candidate module after exact-length patches")
    args = parser.parse_args()
    try:
        report = audit(args.baseline, args.candidate)
    except (OSError, ValueError, struct.error) as exc:
        raise SystemExit(str(exc)) from exc
    output = Path(__file__).resolve().parents[1] / "reports/postlink-ftrace-integrity.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
