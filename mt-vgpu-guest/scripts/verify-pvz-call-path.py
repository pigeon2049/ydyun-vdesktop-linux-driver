#!/usr/bin/env python3
"""Audit static Guest FW raw-heap and PVZ client call paths in Linux 2.3.0."""

from __future__ import annotations

import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0"
ASM = ROOT / "reports/host-2.3-core.asm"
GUEST_PREMAP = ROOT / "scripts/verify-static-guest-fw-premap.py"
FUNCTION = "RGXInitCreateFWKernelMemoryContext"
EXPECTED_RAW_MAP = ["0xba5f1"]
EXPECTED_RAW_UNMAP = ["0xba7b1", "0xba862"]
PVZ_CLIENT_SYMBOLS = ("PvzClientMapDevPhysHeap", "PvzClientUnmapDevPhysHeap")


def function_body(text: str, function: str) -> str:
    marker = f"<{function}>:"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"{function} is missing from saved core disassembly")
    end = text.find("\n000000000", start + 1)
    return text[start:] if end < 0 else text[start:end]


def source_references(source_root: Path = SOURCE) -> list[str]:
    """Find code references outside the prebuilt Linux core object."""
    hits = []
    for pattern in ("*.c", "*.h", "*.S", "Makefile"):
        for path in source_root.rglob(pattern):
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            for symbol in PVZ_CLIENT_SYMBOLS:
                if re.search(rf"\b{symbol}\b", text):
                    hits.append(f"{path.relative_to(source_root)}:{symbol}")
    return sorted(set(hits))


def relocation_sites(text: str, symbol: str) -> list[str]:
    pattern = re.compile(
        rf"^\s*([0-9a-f]+):\s+R_X86_64_[A-Z0-9]+\s+{re.escape(symbol)}(?:[-+][^\s]+)?\s*$",
        re.MULTILINE,
    )
    return [f"0x{int(address, 16):x}" for address in pattern.findall(text)]


def verify_reference_sweep(text: str, source_hits: list[str]) -> dict:
    map_sites = relocation_sites(text, "RGXFwRawHeapAllocMap")
    unmap_sites = relocation_sites(text, "RGXFwRawHeapUnmapFree")
    client_sites = {
        symbol: relocation_sites(text, symbol) for symbol in PVZ_CLIENT_SYMBOLS
    }
    if map_sites != EXPECTED_RAW_MAP:
        raise ValueError(f"unexpected raw FW heap map call sites: {map_sites}")
    if unmap_sites != EXPECTED_RAW_UNMAP:
        raise ValueError(f"unexpected raw FW heap unmap call sites: {unmap_sites}")
    if any(client_sites.values()):
        raise ValueError(f"PVZ client wrapper has static relocation callers: {client_sites}")
    if source_hits:
        raise ValueError(f"official Linux source has PVZ client wrapper references: {source_hits}")

    setup = function_body(text, FUNCTION)
    deinit = function_body(text, "RGXDeInitDestroyFWKernelMemoryContext")
    if any(site[2:] not in setup for site in (map_sites[0], unmap_sites[0])):
        raise ValueError("raw FW heap setup/failure-cleanup relocations changed function")
    if unmap_sites[1][2:] not in deinit:
        raise ValueError("raw FW heap deinit relocation changed function")

    return {
        "raw_fw_heap_map_call_sites": map_sites,
        "raw_fw_heap_unmap_call_sites": unmap_sites,
        "raw_heap_map_call_confined_to_fw_context_setup": True,
        "raw_heap_unmaps_confined_to_setup_failure_and_context_deinit": True,
        "pvz_client_map_static_relocation_sites": client_sites[PVZ_CLIENT_SYMBOLS[0]],
        "pvz_client_unmap_static_relocation_sites": client_sites[PVZ_CLIENT_SYMBOLS[1]],
        "official_linux_source_pvz_client_references": source_hits,
        "scope": "direct ELF relocations and official C/H/S/Makefile sources; opaque indirect or external calls are not ruled out",
    }


def verify(text: str, source_hits: list[str] | None = None) -> dict:
    source_hits = source_references() if source_hits is None else source_hits
    result = verify_reference_sweep(text, source_hits)
    spec = importlib.util.spec_from_file_location("guest_premap_check", GUEST_PREMAP)
    if spec is None or spec.loader is None:
        raise ValueError("could not load static Guest FW premap verifier")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    premap = verifier.verify(text)
    if not premap["guest_branch_bypasses_raw_heap_map"]:
        raise ValueError("Guest firmware path no longer bypasses raw FW heap map")
    deinit = function_body(text, "RGXDeInitDestroyFWKernelMemoryContext")
    if not re.search(
            r"ba843:\s+75 2b\s+jne\s+ba870 <RGXDeInitDestroyFWKernelMemoryContext\+0x50>",
            deinit):
        raise ValueError("Guest deinit mode branch changed")
    if not re.search(r"ba862:\s+R_X86_64_PLT32\s+RGXFwRawHeapUnmapFree-0x4", deinit):
        raise ValueError("Guest deinit no longer skips the Host raw FW heap unmap path")
    result.update({
        "guest_branch_bypasses_raw_heap_map": True,
        "guest_deinit_bypasses_host_raw_heap_unmap": True,
        "guest_branch_sets_fw_premap_status_twice": premap["guest_branch_sets_premap_status_twice"],
        "host_bar2_or_runtime_fw_heap_backing_proven": False,
        "hardware_accessed": False,
    })
    return result


def main() -> None:
    try:
        print(json.dumps(verify(ASM.read_text()), indent=2))
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc))


if __name__ == "__main__":
    main()
