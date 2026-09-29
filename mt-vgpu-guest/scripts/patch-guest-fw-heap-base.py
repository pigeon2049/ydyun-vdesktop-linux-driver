#!/usr/bin/env python3
"""Opt-in fix for the Guest FW_MAIN card-base publication.

Linux Host 2.3.0 publishes its firmware heap base at vgpu_info+0x848. The
prebuilt Guest core first consumes vgpu_info+0x438 as the VVPU segment size for
memory-range calculations, then reuses that size as the FW_MAIN card base.
Changing the first load to +0x848 corrupts the size calculations. This patch
preserves those calculations and instead changes the later Guest-only
mtgpu_platform_data_vz_init copy to read +0x848 through the already loaded info
page pointer. It is an offline candidate pending runtime validation.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import NoReturn


FUNCTION = "mtgpu_platform_data_vz_init"
FUNCTION_OFFSET = 0x8D  # mov rax, [r12+0x10e0] in the Guest-only publication path
OLD = bytes.fromhex("49 8b 84 24 e0 10 00 00")
NEW = bytes.fromhex("48 8b 80 48 08 00 00 90")
GUEST_MODE_BRANCH_OFFSET = 0x63  # je native path; Guest path falls through
GUEST_MODE_BRANCH = bytes.fromhex("0f 84 85 00 00 00")
INFO_PAGE_POINTER_LOAD_OFFSET = 0x81
INFO_PAGE_POINTER_LOAD = bytes.fromhex("49 8b 84 24 38 11 00 00")
INFO_PAGE_POINTER_STORE_OFFSET = 0x89
INFO_PAGE_POINTER_STORE = bytes.fromhex("48 89 43 70")
SIZE_FUNCTION = "mtgpu_device_memory_fixup"
SIZE_FUNCTION_OFFSET = 0x447  # keep mov rcx, [rdx+0x438] as the VVPU-size read
SIZE_LOAD = bytes.fromhex("48 8b 8a 38 04 00 00")
BUILD_TAG = b"5c6c275"


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"FW heap base patch refused: {message}")


def section_table(data: bytes):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail("not an ELF module")
    if data[4] != 2 or data[5] != 1:
        fail("expected little-endian ELF64")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 62:
        fail(f"expected x86-64 ELF machine 62, got {machine}")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if shentsize < 64 or shoff + shentsize * shnum > len(data) or shstrndx >= shnum:
        fail("invalid ELF section table")

    sections = []
    for index in range(shnum):
        fields = struct.unpack_from("<IIQQQQIIQQ", data, shoff + index * shentsize)
        sections.append(fields)

    shstr = sections[shstrndx]
    names = data[shstr[4] : shstr[4] + shstr[5]]

    def cstring(blob: bytes, start: int) -> str:
        end = blob.find(b"\0", start)
        if end < 0:
            fail("unterminated ELF string")
        return blob[start:end].decode("ascii", "replace")

    named = {cstring(names, section[0]): (index, section)
             for index, section in enumerate(sections)}
    if ".text" not in named:
        fail("module has no .text section")
    return sections, named


def find_text_symbol(data: bytes, sections, named, wanted: str) -> tuple[int, int]:
    text_index, text = named[".text"]
    candidates = []
    for section in sections:
        if section[1] not in (2, 11):  # SHT_SYMTAB / SHT_DYNSYM
            continue
        symtab_offset, symtab_size, strtab_index, entsize = section[4], section[5], section[6], section[9]
        if strtab_index >= len(sections) or entsize < 24:
            continue
        strtab = sections[strtab_index]
        strings = data[strtab[4] : strtab[4] + strtab[5]]
        for offset in range(symtab_offset, symtab_offset + symtab_size, entsize):
            if offset + 24 > len(data):
                fail("truncated ELF symbol table")
            name_offset, _info, _other, shndx, value, _size = struct.unpack_from("<IBBHQQ", data, offset)
            if name_offset >= len(strings):
                continue
            name_end = strings.find(b"\0", name_offset)
            if name_end < 0:
                continue
            if strings[name_offset:name_end].decode("ascii", "replace") == wanted and shndx == text_index:
                candidates.append((value, text[3]))
    if not candidates:
        fail(f"{wanted} symbol is missing from .text")
    if len(set(candidates)) != 1:
        fail(f"{wanted} symbol is ambiguous")
    return candidates[0]


def patch_instruction(data: bytes, location: int) -> bytes:
    current = data[location : location + len(OLD)]
    if current == NEW:
        fail("module is already patched")
    if current != OLD:
        fail(f"unexpected instruction bytes at file offset {location:#x}: {current.hex()}")
    return data[:location] + NEW + data[location + len(OLD) :]


def validate_guest_publication_context(data: bytes, function_file_offset: int) -> None:
    """Refuse to patch if this is no longer the known Guest-only data flow."""
    expected = (
        (GUEST_MODE_BRANCH_OFFSET, GUEST_MODE_BRANCH, "Guest-mode branch"),
        (INFO_PAGE_POINTER_LOAD_OFFSET, INFO_PAGE_POINTER_LOAD, "information-page pointer load"),
        (INFO_PAGE_POINTER_STORE_OFFSET, INFO_PAGE_POINTER_STORE, "published information-page pointer"),
    )
    for offset, instruction, label in expected:
        location = function_file_offset + offset
        actual = data[location : location + len(instruction)]
        if actual != instruction:
            fail(f"{label} changed at function offset {offset:#x}: {actual.hex()}")


def text_instruction_location(data: bytes, sections, named, function: str,
                              function_offset: int) -> int:
    symbol_value, text_addr = find_text_symbol(data, sections, named, function)
    if symbol_value < text_addr:
        fail(f"{function} symbol address is outside .text")
    return named[".text"][1][4] + (symbol_value - text_addr) + function_offset


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
    args = parser.parse_args()
    path = args.module
    data = path.read_bytes()
    if BUILD_TAG not in data:
        fail("module does not contain the expected 5c6c275 build tag")
    sections, named = section_table(data)
    location = text_instruction_location(data, sections, named, FUNCTION, FUNCTION_OFFSET)
    function_start = text_instruction_location(data, sections, named, FUNCTION, 0)
    validate_guest_publication_context(data, function_start)
    size_load_location = text_instruction_location(
        data, sections, named, SIZE_FUNCTION, SIZE_FUNCTION_OFFSET)
    if data[size_load_location : size_load_location + len(SIZE_LOAD)] != SIZE_LOAD:
        fail("the VVPU-size load changed; refusing a patch against an unknown size-calculation path")
    patched = patch_instruction(data, location)

    # Same-length instruction replacement preserves ELF offsets and metadata.
    with path.open("r+b") as module:
        module.seek(location)
        module.write(patched[location : location + len(NEW)])
        module.flush()
    if path.read_bytes() != patched:
        fail("post-write verification failed")
    final = path.read_bytes()
    if final[size_load_location : size_load_location + len(SIZE_LOAD)] != SIZE_LOAD:
        fail("post-write verification found the VVPU-size load changed")
    print(f"patched {path}: {FUNCTION}+{FUNCTION_OFFSET:#x} {OLD.hex()} -> {NEW.hex()}")
    print("Guest FW_MAIN sCardBase now reads Linux vgpu_info.fw_heap_base at 0x848")
    print(f"Preserved {SIZE_FUNCTION}+{SIZE_FUNCTION_OFFSET:#x} VVPU-size load and VPU memory-range calculations")
    print("Candidate is offline only; runtime heap mapping and acceleration are unverified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
