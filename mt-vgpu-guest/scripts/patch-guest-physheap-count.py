#!/usr/bin/env python3
"""Apply a Guest-branch-only x86-64 5c6c275 heap-count correction.

The observed Local Guest produces four heap descriptors while the core expects
five; static analysis predicts the same mismatch for HOST-memory Guest. Rather
than changing the shared ``add $6,%r13d`` used by every driver mode, redirect
the Guest-mode conditional branch through the function's verified 10-byte
alignment NOP. The trampoline decrements the expected count, then rejoins the
Guest block. Non-Guest flow retains the original count. Hybrid Guest remains
topology-dependent and is not thereby validated. The patch changes only bytes
inside the already linked SysDevInit body; section sizes and ftrace metadata do
not move. It remains opt-in pending runtime validation.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import NoReturn


BASE_COUNT_OFFSET = 0x92  # shared add $6,%r13d, deliberately left unchanged
BASE_COUNT = bytes.fromhex("41 83 c5 06")
GUEST_BRANCH_OFFSET = 0x25B  # jne from get_driver_mode result to Guest block
GUEST_BRANCH_OLD = bytes.fromhex("0f 85 af 00 00 00")
GUEST_BLOCK_OFFSET = 0x310
TRAMPOLINE_OFFSET = 0x8B6  # 10-byte alignment NOP immediately before next symbol
TRAMPOLINE_OLD = bytes.fromhex("66 2e 0f 1f 84 00 00 00 00 00")
TRAMPOLINE_SIZE = len(TRAMPOLINE_OLD)
TRAMPOLINE_PREFIX = bytes.fromhex("41 83 ed 01")  # sub $1,%r13d
TRAMPOLINE_JUMP_OPCODE = b"\xe9"
TRAMPOLINE_SUFFIX = b"\x90"
GUEST_BRANCH_NEW = bytes.fromhex("0f 85 55 06 00 00")
TRAMPOLINE_NEW = bytes.fromhex("41 83 ed 01 e9 51 fa ff ff 90")
BUILD_TAG = b"5c6c275"


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"physheap patch refused: {message}")


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

    named = {cstring(names, section[0]): (index, section) for index, section in enumerate(sections)}
    if ".text" not in named:
        fail("module has no .text section")
    return sections, named


def find_sysdevinit(data: bytes, sections, named) -> tuple[int, int]:
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
            if strings[name_offset:name_end] == b"SysDevInit" and shndx == text_index:
                candidates.append((value, text[3]))
    if not candidates:
        fail("SysDevInit symbol is missing from .text")
    if len(set(candidates)) != 1:
        fail("SysDevInit symbol is ambiguous")
    return candidates[0]


def _rel32(source_after_instruction: int, target: int) -> bytes:
    displacement = target - source_after_instruction
    if not -(1 << 31) <= displacement < (1 << 31):
        raise ValueError("Guest heap-count trampoline branch is out of rel32 range")
    return struct.pack("<i", displacement)


def patch_sysdevinit_body(body: bytes) -> bytes:
    """Patch an isolated SysDevInit body and preserve its shared base count."""
    minimum = TRAMPOLINE_OFFSET + TRAMPOLINE_SIZE
    if len(body) < minimum:
        raise ValueError("SysDevInit body is shorter than the guarded trampoline site")
    if body[BASE_COUNT_OFFSET:BASE_COUNT_OFFSET + len(BASE_COUNT)] != BASE_COUNT:
        raise ValueError("shared SysDevInit base-count instruction changed")
    branch = body[GUEST_BRANCH_OFFSET:GUEST_BRANCH_OFFSET + len(GUEST_BRANCH_OLD)]
    if branch != GUEST_BRANCH_OLD:
        raise ValueError(f"Guest-mode branch changed: {branch.hex()}")
    original_target = (GUEST_BRANCH_OFFSET + len(GUEST_BRANCH_OLD) +
                       struct.unpack_from("<i", branch, 2)[0])
    if original_target != GUEST_BLOCK_OFFSET:
        raise ValueError("Guest-mode branch no longer targets the audited Guest block")
    cave = body[TRAMPOLINE_OFFSET:TRAMPOLINE_OFFSET + TRAMPOLINE_SIZE]
    if cave != TRAMPOLINE_OLD:
        raise ValueError(f"SysDevInit trampoline space is not the expected NOP: {cave.hex()}")

    branch_new = (GUEST_BRANCH_OLD[:2] +
                  _rel32(GUEST_BRANCH_OFFSET + len(GUEST_BRANCH_OLD), TRAMPOLINE_OFFSET))
    trampoline_jump_offset = TRAMPOLINE_OFFSET + len(TRAMPOLINE_PREFIX)
    trampoline_next = trampoline_jump_offset + 5
    trampoline_new = (TRAMPOLINE_PREFIX + TRAMPOLINE_JUMP_OPCODE +
                      _rel32(trampoline_next, GUEST_BLOCK_OFFSET) + TRAMPOLINE_SUFFIX)
    if branch_new != GUEST_BRANCH_NEW or trampoline_new != TRAMPOLINE_NEW:
        raise ValueError("computed Guest trampoline differs from the audited instruction sequence")

    patched = bytearray(body)
    patched[GUEST_BRANCH_OFFSET:GUEST_BRANCH_OFFSET + len(branch_new)] = branch_new
    patched[TRAMPOLINE_OFFSET:TRAMPOLINE_OFFSET + len(trampoline_new)] = trampoline_new
    return bytes(patched)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
    args = parser.parse_args()
    path = args.module
    data = path.read_bytes()
    if BUILD_TAG not in data:
        fail("module does not contain the expected 5c6c275 build tag")
    sections, named = section_table(data)
    symbol_value, text_addr = find_sysdevinit(data, sections, named)
    text_index, text = named[".text"]
    if symbol_value < text_addr:
        fail("SysDevInit symbol address is outside .text")
    function_offset = symbol_value - text_addr
    body_file_offset = text[4] + function_offset
    body_size = TRAMPOLINE_OFFSET + TRAMPOLINE_SIZE
    body = data[body_file_offset:body_file_offset + body_size]
    try:
        patched = patch_sysdevinit_body(body)
    except ValueError as exc:
        fail(str(exc))

    branch_file_offset = body_file_offset + GUEST_BRANCH_OFFSET
    trampoline_file_offset = body_file_offset + TRAMPOLINE_OFFSET
    # Exact-length writes: section offsets, relocations and ftrace tables stay intact.
    with path.open("r+b") as module:
        module.seek(branch_file_offset)
        module.write(patched[GUEST_BRANCH_OFFSET:GUEST_BRANCH_OFFSET + len(GUEST_BRANCH_OLD)])
        module.seek(trampoline_file_offset)
        module.write(patched[TRAMPOLINE_OFFSET:TRAMPOLINE_OFFSET + TRAMPOLINE_SIZE])
        module.flush()

    patched = path.read_bytes()
    if (patched[branch_file_offset:branch_file_offset + len(GUEST_BRANCH_NEW)] != GUEST_BRANCH_NEW or
            patched[trampoline_file_offset:trampoline_file_offset + TRAMPOLINE_SIZE] != TRAMPOLINE_NEW):
        fail("post-write verification failed")
    if patched[body_file_offset + BASE_COUNT_OFFSET:
               body_file_offset + BASE_COUNT_OFFSET + len(BASE_COUNT)] != BASE_COUNT:
        fail("shared base count unexpectedly changed")
    print(f"patched {path}: Guest branch -> SysDevInit+{TRAMPOLINE_OFFSET:#x} trampoline")
    print(f"Guest-only decrement at +{TRAMPOLINE_OFFSET:#x}; shared add $6 at +{BASE_COUNT_OFFSET:#x} unchanged")
    print("Local Guest: observed generated=4, expected 5 -> 4")
    print("HOST-memory Guest: static path predicts generated=4, expected 5 -> 4")
    print("Hybrid Guest: count=osid_count+2; topology still requires separate validation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
