#!/usr/bin/env python3
"""Retarget one guarded ELF relocation to the Guest VPU heap-alias wrapper."""

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
CALLER = "PVRSRVCommonDeviceCreate"
ORIGINAL = "PVRSRVPhysMemHeapsInit"
WRAPPER = "mtgpu_guest_vpu_heap_alias_init"
CALL_OFFSET = 0x1E8
SHT_RELA = 4
R_X86_64_PLT32 = 4


def fail(message: str) -> "NoReturn":
	raise SystemExit(f"Guest VPU heap-alias call patch refused: {message}")


def load_elf_helpers():
	helper_path = Path(__file__).with_name("patch-guest-physheap-count.py")
	spec = importlib.util.spec_from_file_location("guest_physheap_count_patch", helper_path)
	if spec is None or spec.loader is None:
		fail("cannot load the ELF section helpers")
	helper = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = helper
	spec.loader.exec_module(helper)
	return helper


def find_symbol(data: bytes, sections, name: str) -> tuple[int, int, int]:
	"""Return symbol-table section index, symbol index, and .text value."""
	matches = []
	for table_index, section in enumerate(sections):
		if section[1] not in (2, 11):
			continue
		strtab_index = section[6]
		if strtab_index >= len(sections) or section[9] < 24:
			continue
		strings_section = sections[strtab_index]
		strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
		for symbol_index, offset in enumerate(range(section[4], section[4] + section[5], section[9])):
			if offset + 24 > len(data):
				fail("truncated ELF symbol table")
			name_offset, _info, _other, shndx, value, _size = struct.unpack_from(
				"<IBBHQQ", data, offset)
			if name_offset >= len(strings):
				continue
			end = strings.find(b"\0", name_offset)
			if end >= 0 and strings[name_offset:end].decode("ascii", "replace") == name:
				matches.append((table_index, symbol_index, value, shndx))
	if not matches:
		fail(f"required symbol {name} is missing")
	text_matches = [item for item in matches if item[3] == 1 and sections[item[0]][1] == 2]
	if len(text_matches) != 1:
		fail(f"required .text symbol {name} is ambiguous or missing")
	table_index, symbol_index, value, _ = text_matches[0]
	return table_index, symbol_index, value


def find_relocation(data: bytes, sections, target_section: int, symtab_index: int,
				relocation_offset: int, expected_symbol_index: int) -> tuple[int, int]:
	"""Return file offset and r_info for the unique guarded PLT32 relocation."""
	matches = []
	for section in sections:
		if (section[1] != SHT_RELA or section[7] != target_section or
				section[6] != symtab_index or section[9] < 24):
			continue
		for entry in range(section[4], section[4] + section[5], section[9]):
			if entry + 24 > len(data):
				fail("truncated ELF relocation table")
			offset, info, addend = struct.unpack_from("<QQq", data, entry)
			if offset != relocation_offset:
				continue
			symbol_index, relocation_type = info >> 32, info & 0xFFFFFFFF
			if (symbol_index, relocation_type, addend) != (expected_symbol_index, R_X86_64_PLT32, -4):
				fail("call relocation no longer targets the audited original PLT32 symbol")
			matches.append((entry + 8, info))
	if len(matches) != 1:
		fail(f"expected one call relocation at {relocation_offset:#x}, found {len(matches)}")
	return matches[0]


def redirected_info(old_info: int, expected_symbol_index: int,
				new_symbol_index: int) -> int:
	old_symbol_index, relocation_type = old_info >> 32, old_info & 0xFFFFFFFF
	if old_symbol_index != expected_symbol_index or relocation_type != R_X86_64_PLT32:
		raise ValueError("relocation target or type changed")
	return (new_symbol_index << 32) | relocation_type


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
	args = parser.parse_args()
	path = args.module
	data = path.read_bytes()
	if BUILD_TAG not in data:
		fail("module does not contain the expected 5c6c275 core build tag")

	helper = load_elf_helpers()
	sections, named = helper.section_table(data)
	text_index, text = named[".text"]
	caller_table, _caller_index, caller = find_symbol(data, sections, CALLER)
	original_table, original_index, original = find_symbol(data, sections, ORIGINAL)
	wrapper_table, wrapper_index, wrapper = find_symbol(data, sections, WRAPPER)
	if caller_table != original_table or caller_table != wrapper_table:
		fail("audited symbols are not present in one common symbol table")
	if caller < text[3] or original < text[3] or wrapper < text[3]:
		fail("audited function symbol is outside .text")
	relocation_site = caller + CALL_OFFSET + 1  # R_X86_64_PLT32 covers CALL's rel32 field
	relocation_file_offset, old_info = find_relocation(
		data, sections, text_index, caller_table, relocation_site, original_index)
	try:
		new_info = redirected_info(old_info, original_index, wrapper_index)
	except ValueError as exc:
		fail(str(exc))

	with path.open("r+b") as module:
		module.seek(relocation_file_offset)
		module.write(struct.pack("<Q", new_info))
		module.flush()
	result = path.read_bytes()
	if struct.unpack_from("<Q", result, relocation_file_offset)[0] != new_info:
		fail("post-write relocation verification failed")
	if (result[:relocation_file_offset] != data[:relocation_file_offset] or
			result[relocation_file_offset + 8:] != data[relocation_file_offset + 8:]):
		fail("unexpected module bytes changed outside the relocation symbol index")
	report = {
		"module": str(path.resolve()),
		"sha256": hashlib.sha256(result).hexdigest(),
		"caller": CALLER,
		"call_offset": hex(CALL_OFFSET),
		"relocation_offset": hex(relocation_site),
		"original_target": ORIGINAL,
		"original_symbol_index": original_index,
		"wrapper_target": WRAPPER,
		"wrapper_symbol_index": wrapper_index,
		"only_relocation_symbol_index_changed": True,
		"scope": f"reroutes the audited {CALLER} call to {ORIGINAL} only",
	}
	report_path = path.parent / "guest-vpu-heap-alias-call-validation.json"
	report_path.write_text(json.dumps(report, indent=2) + "\n")
	print(json.dumps(report, indent=2))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
