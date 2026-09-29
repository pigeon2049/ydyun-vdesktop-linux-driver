#!/usr/bin/env python3
"""Opt-in dual-version Guest vGPU info-page ABI adaptation.

Routes the FW_MAIN heap-base publication and VPU shared-memory address/size
through strict v1/v2 decoders, reads the common BAR2 actual-memory-size field
for the VPU range path, and expands the zeroed info-page buffer for the V2
layout. The writes preserve instruction lengths and are only intended for the
matching 2.3.0 Guest-only build; they do not modify or load a system module.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import NoReturn


FUNCTION = "mtgpu_platform_data_vz_init"
FUNCTION_OFFSET = 0x8D
OLD_FW_LOAD = bytes.fromhex("49 8b 84 24 e0 10 00 00")
FW_HELPER_CALL_PREFIX = bytes.fromhex("48 89 c7 e8")
GUEST_MODE_BRANCH_OFFSET = 0x63
GUEST_MODE_BRANCH = bytes.fromhex("0f 84 85 00 00 00")
INFO_PAGE_POINTER_LOAD_OFFSET = 0x81
INFO_PAGE_POINTER_LOAD = bytes.fromhex("49 8b 84 24 38 11 00 00")
INFO_PAGE_POINTER_STORE_OFFSET = 0x89
INFO_PAGE_POINTER_STORE = bytes.fromhex("48 89 43 70")
SIZE_FUNCTION = "mtgpu_device_memory_fixup"
SIZE_FUNCTION_OFFSET = 0x447
OLD_SIZE_LOAD = bytes.fromhex("48 8b 8a 38 04 00 00")
NEW_SIZE_LOAD = bytes.fromhex("48 8b 4a 20 90 90 90")
SHARE_OFFSET = 0x390
SHARE_SIZE_OFFSET = 0x3A3
OLD_SHARE_SETUP = bytes.fromhex(
    "49 8b 94 24 38 11 00 00 "
    "48 8b 92 48 04 00 00 "
    "48 89 50 10"
)
OLD_SHARE_SIZE = bytes.fromhex("48 c7 40 20 00 80 00 00")
NEW_SHARE_SIZE = b"\x90" * len(OLD_SHARE_SIZE)
SHARE_CALL_PREFIX = bytes.fromhex("48 89 c7 4c 89 e6 e8")
SHARE_HELPER = "mtgpu_guest_vpu_share_mem_addr_compat"
INFO_ALLOCATION_OFFSET = 0x24A
INFO_ALLOCATION_OLD = bytes.fromhex("48 8d bb 9f 08 00 00")
INFO_ALLOCATION_NEW = bytes.fromhex("48 8d bb ff 0f 00 00")
VPU_FLAG_BRANCH_OFFSET = 0x299
VPU_FLAG_BRANCH = bytes.fromhex("48 8b 42 10 a8 01 0f 85 a2 01 00 00")
FW_HEAP_SIZE = 0x800000  # RGX_FW_HEAP_SHIFT=23; fixed by the matching 2.3.0 Host
FW_HEAP_SIZE_READS = (
    (0x2AD, bytes.fromhex("8b 82 50 08 00 00"), b"\xb8" + struct.pack("<I", FW_HEAP_SIZE) + b"\x90"),
    (0x2CD, bytes.fromhex("8b 92 50 08 00 00"), b"\xba" + struct.pack("<I", FW_HEAP_SIZE) + b"\x90"),
    (0x312, bytes.fromhex("8b 90 50 08 00 00"), b"\xba" + struct.pack("<I", FW_HEAP_SIZE) + b"\x90"),
)
BUILD_TAG = b"5c6c275"


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"vGPU info compatibility patch refused: {message}")


def section_table(data: bytes):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail("not an ELF module")
    if data[4] != 2 or data[5] != 1:
        fail("expected little-endian ELF64")
    if struct.unpack_from("<H", data, 18)[0] != 62:
        fail("expected x86-64 ELF machine 62")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    if shentsize < 64 or shoff + shentsize * shnum > len(data) or shstrndx >= shnum:
        fail("invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
                for i in range(shnum)]
    string_section = sections[shstrndx]
    names = data[string_section[4]:string_section[4] + string_section[5]]

    def cstring(blob: bytes, start: int) -> str:
        end = blob.find(b"\0", start)
        if end < 0:
            fail("unterminated ELF string")
        return blob[start:end].decode("ascii", "replace")

    named = {cstring(names, section[0]): (i, section)
             for i, section in enumerate(sections)}
    if ".text" not in named:
        fail("module has no .text section")
    return sections, named


def find_text_symbol(data: bytes, sections, named, wanted: str) -> tuple[int, int, int]:
    text_index, text = named[".text"]
    candidates = []
    for section in sections:
        if section[1] not in (2, 11):  # SHT_SYMTAB / SHT_DYNSYM
            continue
        symoff, symsize, strtab_index, entsize = section[4], section[5], section[6], section[9]
        if strtab_index >= len(sections) or entsize < 24:
            continue
        strtab = sections[strtab_index]
        strings = data[strtab[4]:strtab[4] + strtab[5]]
        for offset in range(symoff, symoff + symsize, entsize):
            if offset + 24 > len(data):
                fail("truncated ELF symbol table")
            name_offset, _info, _other, shndx, value, _size = struct.unpack_from(
                "<IBBHQQ", data, offset)
            if name_offset >= len(strings):
                continue
            name_end = strings.find(b"\0", name_offset)
            if name_end < 0:
                continue
            if strings[name_offset:name_end].decode("ascii", "replace") == wanted and shndx == text_index:
                candidates.append((value, text[3], text[4]))
    if not candidates:
        fail(f"{wanted} symbol is missing from .text")
    if len(set(candidates)) != 1:
        fail(f"{wanted} symbol is ambiguous")
    return candidates[0]


def instruction_location(symbol: tuple[int, int, int], offset: int) -> tuple[int, int]:
    value, text_addr, text_offset = symbol
    if value < text_addr:
        fail("symbol address is outside .text")
    return text_offset + value - text_addr + offset, value + offset


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
    args = parser.parse_args()
    path = args.module
    data = path.read_bytes()
    if BUILD_TAG not in data:
        fail("module does not contain the expected 5c6c275 build tag")
    sections, named = section_table(data)
    platform = find_text_symbol(data, sections, named, FUNCTION)
    helper = find_text_symbol(data, sections, named, "mtgpu_guest_fw_heap_base_compat")
    share_helper = find_text_symbol(data, sections, named, SHARE_HELPER)
    size_function = find_text_symbol(data, sections, named, SIZE_FUNCTION)
    function_file, function_address = instruction_location(platform, 0)
    site_file, site_address = instruction_location(platform, FUNCTION_OFFSET)
    size_file, _size_address = instruction_location(size_function, SIZE_FUNCTION_OFFSET)
    share_file, share_address = instruction_location(size_function, SHARE_OFFSET)
    share_size_file, _share_size_address = instruction_location(
        size_function, SHARE_SIZE_OFFSET)
    allocation_file, _allocation_address = instruction_location(
        size_function, INFO_ALLOCATION_OFFSET)
    branch_file, _branch_address = instruction_location(size_function, VPU_FLAG_BRANCH_OFFSET)
    if data[branch_file:branch_file + len(VPU_FLAG_BRANCH)] != VPU_FLAG_BRANCH:
        fail("VPU flag branch changed; cannot prove when the legacy +0x850 reads execute")

    size_read_locations = []
    for offset, old, new in FW_HEAP_SIZE_READS:
        location, _address = instruction_location(size_function, offset)
        current = data[location:location + len(old)]
        if current == new:
            fail(f"fw_heap_size read at {SIZE_FUNCTION}+{offset:#x} is already patched")
        if current != old:
            fail(f"unexpected fw_heap_size instruction at {SIZE_FUNCTION}+{offset:#x}: {current.hex()}")
        size_read_locations.append((offset, location, old, new))

    helper_address = helper[0]

    for offset, expected, label in (
        (GUEST_MODE_BRANCH_OFFSET, GUEST_MODE_BRANCH, "Guest-mode branch"),
        (INFO_PAGE_POINTER_LOAD_OFFSET, INFO_PAGE_POINTER_LOAD, "information-page load"),
        (INFO_PAGE_POINTER_STORE_OFFSET, INFO_PAGE_POINTER_STORE, "published info-page pointer"),
    ):
        actual = data[function_file + offset:function_file + offset + len(expected)]
        if actual != expected:
            fail(f"{label} changed at {FUNCTION}+{offset:#x}: {actual.hex()}")
    if data[site_file:site_file + len(OLD_FW_LOAD)] != OLD_FW_LOAD:
        fail("FW base publication instruction is not the expected unpatched load")
    size_before = data[size_file:size_file + len(OLD_SIZE_LOAD)]
    if size_before == NEW_SIZE_LOAD:
        fail("module is already patched")
    if size_before != OLD_SIZE_LOAD:
        fail(f"VPU memory-size load changed: {size_before.hex()}")
    share_before = data[share_file:share_file + len(OLD_SHARE_SETUP)]
    if share_before != OLD_SHARE_SETUP:
        fail(f"VPU shared-memory setup changed at {SIZE_FUNCTION}+{SHARE_OFFSET:#x}: {share_before.hex()}")
    share_size_before = data[share_size_file:share_size_file + len(OLD_SHARE_SIZE)]
    if share_size_before == NEW_SHARE_SIZE:
        fail("VPU shared-memory size store is already delegated to the v1/v2 helper")
    if share_size_before != OLD_SHARE_SIZE:
        fail(f"unexpected VPU shared-memory size store at {SIZE_FUNCTION}+{SHARE_SIZE_OFFSET:#x}: {share_size_before.hex()}")
    allocation_before = data[allocation_file:allocation_file + len(INFO_ALLOCATION_OLD)]
    if allocation_before == INFO_ALLOCATION_NEW:
        fail("vGPU information-page allocation is already expanded")
    if allocation_before != INFO_ALLOCATION_OLD:
        fail(f"unexpected information-page allocation at {SIZE_FUNCTION}+{INFO_ALLOCATION_OFFSET:#x}: {allocation_before.hex()}")

    # `mov rdi, rax; call rel32` replaces the existing eight-byte load. The
    # call's next RIP is site+8, so the relative displacement stays in range.
    displacement = helper_address - (site_address + 8)
    if displacement < -(1 << 31) or displacement >= (1 << 31):
        fail("decoder helper is outside rel32 call range")
    fw_replacement = FW_HELPER_CALL_PREFIX + struct.pack("<i", displacement)
    if len(fw_replacement) != len(OLD_FW_LOAD):
        fail("internal error: FW patch changed instruction-window length")

    share_displacement = share_helper[0] - (share_address + len(SHARE_CALL_PREFIX) + 4)
    if share_displacement < -(1 << 31) or share_displacement >= (1 << 31):
        fail("shared-memory compatibility helper is outside rel32 call range")
    share_replacement = (SHARE_CALL_PREFIX + struct.pack("<i", share_displacement) +
                         b"\x90" * (len(OLD_SHARE_SETUP) - len(SHARE_CALL_PREFIX) - 4))
    if len(share_replacement) != len(OLD_SHARE_SETUP):
        fail("internal error: VPU shared-memory patch changed instruction-window length")

    patched = bytearray(data)
    patched[site_file:site_file + len(fw_replacement)] = fw_replacement
    patched[size_file:size_file + len(NEW_SIZE_LOAD)] = NEW_SIZE_LOAD
    patched[share_file:share_file + len(share_replacement)] = share_replacement
    patched[share_size_file:share_size_file + len(NEW_SHARE_SIZE)] = NEW_SHARE_SIZE
    patched[allocation_file:allocation_file + len(INFO_ALLOCATION_NEW)] = INFO_ALLOCATION_NEW
    for _offset, location, _old, new in size_read_locations:
        patched[location:location + len(new)] = new
    with path.open("r+b") as module:
        module.seek(site_file)
        module.write(fw_replacement)
        module.seek(size_file)
        module.write(NEW_SIZE_LOAD)
        module.seek(share_file)
        module.write(share_replacement)
        module.seek(share_size_file)
        module.write(NEW_SHARE_SIZE)
        module.seek(allocation_file)
        module.write(INFO_ALLOCATION_NEW)
        for _offset, location, _old, new in size_read_locations:
            module.seek(location)
            module.write(new)
        module.flush()
    final = path.read_bytes()
    if final != bytes(patched):
        fail("post-write verification failed")
    if final[site_file:site_file + 3] != bytes.fromhex("48 89 c7"):
        fail("post-write verification found a bad helper argument move")
    call_displacement = struct.unpack_from("<i", final, site_file + 4)[0]
    if site_address + 8 + call_displacement != helper_address:
        fail("post-write verification found a bad helper call target")
    if final[size_file:size_file + len(NEW_SIZE_LOAD)] != NEW_SIZE_LOAD:
        fail("post-write verification found a bad BAR2-size load")
    if final[share_file:share_file + len(share_replacement)] != share_replacement:
        fail("post-write verification found a bad VPU shared-memory helper call")
    if final[share_size_file:share_size_file + len(NEW_SHARE_SIZE)] != NEW_SHARE_SIZE:
        fail("post-write verification found a bad VPU shared-memory size handoff")
    if final[allocation_file:allocation_file + len(INFO_ALLOCATION_NEW)] != INFO_ALLOCATION_NEW:
        fail("post-write verification found a bad information-page allocation size")
    share_call_displacement = struct.unpack_from(
        "<i", final, share_file + len(SHARE_CALL_PREFIX))[0]
    if share_address + len(SHARE_CALL_PREFIX) + 4 + share_call_displacement != share_helper[0]:
        fail("post-write verification found a bad VPU shared-memory helper target")
    for offset, location, _old, new in size_read_locations:
        if final[location:location + len(new)] != new:
            fail(f"post-write verification failed at {SIZE_FUNCTION}+{offset:#x}")
    print(f"patched {path}: {FUNCTION}+{FUNCTION_OFFSET:#x} now calls the v1/v2 FW heap decoder")
    print(f"patched {path}: {SIZE_FUNCTION}+{SIZE_FUNCTION_OFFSET:#x} reads common info-page +0x20")
    print(f"patched {path}: {SIZE_FUNCTION}+{SHARE_OFFSET:#x} resolves the V1/V2 VPU share address")
    print(f"patched {path}: {SIZE_FUNCTION}+{SHARE_SIZE_OFFSET:#x} delegates V1/V2 share size to the helper")
    print(f"patched {path}: {SIZE_FUNCTION}+{INFO_ALLOCATION_OFFSET:#x} allocates a 4 KiB vGPU info page for V2 fields")
    print(f"patched {path}: three legacy fw_heap_size loads now use fixed 8 MiB ({FW_HEAP_SIZE:#x})")
    print("Same-length edits preserve ELF and ftrace offsets; hardware behavior remains unverified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
