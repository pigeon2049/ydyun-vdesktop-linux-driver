#!/usr/bin/env python3
"""Redirect audited platform-data calls through the Guest BAR2 MMU fallback."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path
from typing import NoReturn


BUILD_TAG = b"5c6c275"
ORIGINAL = "mtgpu_platform_data_vz_init"
WRAPPER = "mtgpu_guest_vz_platform_data_init"
CALLERS = (
	"quyuan1_get_platform_device_info",
	"quyuan2_get_platform_device_info",
	"sudi_get_platform_device_info",
)
SHT_RELA = 4
R_X86_64_PLT32 = 4


def fail(message: str) -> "NoReturn":
	raise SystemExit(f"Guest BAR2 MMU fallback patch refused: {message}")


def load_elf_helpers():
	helper_path = Path(__file__).with_name("patch-guest-physheap-count.py")
	spec = importlib.util.spec_from_file_location("guest_physheap_count_patch", helper_path)
	if spec is None or spec.loader is None:
		fail("cannot load the ELF section helpers")
	helper = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = helper
	spec.loader.exec_module(helper)
	return helper


def find_symbols(data: bytes, sections, text_index: int, names: set[str]):
	matches = {name: [] for name in names}
	for table_index, section in enumerate(sections):
		if section[1] != 2 or section[9] < 24:
			continue
		strtab_index = section[6]
		if strtab_index >= len(sections):
			fail("symbol table string section is out of range")
		strings_section = sections[strtab_index]
		strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
		for symbol_index, offset in enumerate(range(section[4], section[4] + section[5], section[9])):
			if offset + 24 > len(data):
				fail("truncated ELF symbol table")
			name_offset, _info, _other, shndx, value, size = struct.unpack_from(
				"<IBBHQQ", data, offset)
			if name_offset >= len(strings):
				continue
			end = strings.find(b"\0", name_offset)
			if end < 0:
				continue
			name = strings[name_offset:end].decode("ascii", "replace")
			if name in matches and shndx == text_index:
				matches[name].append((table_index, symbol_index, value, size))

	result = {}
	for name, entries in matches.items():
		if len(entries) != 1:
			fail(f"required .text symbol {name} is ambiguous or missing")
		result[name] = entries[0]
	return result


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
	parser.add_argument("--audit-only", action="store_true", help="require the diagnostic stop wrapper")
	args = parser.parse_args()
	path = args.module
	original_data = path.read_bytes()
	if not args.audit_only:
		fail("BAR2 fallback withdrawn; only the diagnostic stop wrapper is permitted")
	if b"MT_HEAP_AUDIT stopping before PVRSRVPhysMemHeapsInit/MMU allocation" not in original_data:
		fail("diagnostic stop wrapper is missing")
	if BUILD_TAG not in original_data:
		fail("module does not contain the expected 5c6c275 core build tag")

	helper = load_elf_helpers()
	sections, named = helper.section_table(original_data)
	text_index, _text = named[".text"]
	names = {ORIGINAL, WRAPPER, *CALLERS}
	symbols = find_symbols(original_data, sections, text_index, names)
	table_indices = {record[0] for record in symbols.values()}
	if len(table_indices) != 1:
		fail("audited functions do not share a symbol table")
	symtab_index = next(iter(table_indices))
	original_index = symbols[ORIGINAL][1]
	wrapper_index = symbols[WRAPPER][1]
	caller_ranges = {
		name: (symbols[name][2], symbols[name][2] + symbols[name][3])
		for name in CALLERS
	}

	call_sites = {name: [] for name in CALLERS}
	for section in sections:
		if (section[1] != SHT_RELA or section[7] != text_index or
				section[6] != symtab_index or section[9] < 24):
			continue
		for entry in range(section[4], section[4] + section[5], section[9]):
			if entry + 24 > len(original_data):
				fail("truncated ELF relocation table")
			offset, info, addend = struct.unpack_from("<QQq", original_data, entry)
			symbol_index, relocation_type = info >> 32, info & 0xFFFFFFFF
			if symbol_index != original_index:
				continue
			if (relocation_type, addend) != (R_X86_64_PLT32, -4):
				fail(f"unexpected relocation at {offset:#x}")
			owners = [name for name, (start, end) in caller_ranges.items()
				  if start <= offset - 1 < end]
			if len(owners) != 1:
				continue
			caller = owners[0]
			call_sites[caller].append((offset, entry + 8, info))

	for caller, entries in call_sites.items():
		if len(entries) != 1:
			fail(f"expected one {ORIGINAL} call in {caller}, found {len(entries)}")

	patched_data = bytearray(original_data)
	changes = []
	for caller in CALLERS:
		offset, info_file_offset, old_info = call_sites[caller][0]
		new_info = (wrapper_index << 32) | (old_info & 0xFFFFFFFF)
		struct.pack_into("<Q", patched_data, info_file_offset, new_info)
		changes.append({
			"caller": caller,
			"call_offset": hex(offset - 1 - caller_ranges[caller][0]),
			"relocation_offset": hex(offset),
			"original_symbol_index": original_index,
			"wrapper_symbol_index": wrapper_index,
		})

	result = bytes(patched_data)
	allowed_offsets = {entry[1] for entries in call_sites.values() for entry in entries}
	for index, (before, after) in enumerate(zip(original_data, result)):
		if before == after:
			continue
		if not any(start <= index < start + 8 for start in allowed_offsets):
			fail(f"unexpected byte changed at file offset {index:#x}")

	path.write_bytes(result)
	if path.read_bytes() != result:
		fail("post-write module verification failed")
	report = {
		"module": str(path.resolve()),
		"sha256": hashlib.sha256(result).hexdigest(),
		"original_target": ORIGINAL,
		"wrapper_target": WRAPPER,
		"call_sites": changes,
		"only_relocation_symbol_indices_changed": True,
		"scope": "Diagnostic platform snapshot; heap wrapper stops before MMU allocation; no BAR2 fallback",
	}
	report_path = path.parent / "guest-heap-audit-relocation-validation.json"
	report_path.write_text(json.dumps(report, indent=2) + "\n")
	print(json.dumps(report, indent=2))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
