#!/usr/bin/env python3
"""Map the Guest VPU physheap to the group requested by the Linux PVR core.

Runtime tracing on the current S3000 Guest showed that
PVRSRVPhysMemHeapsInit() requests PHYS_HEAP_VPU_GROUP1 (ID 28), but the Guest
descriptor in the 5c6c275 core was tagged PHYS_HEAP_VPU_GROUP2 (ID 29). This
patch changes only that descriptor's usage flag. It does not establish that
the mapped 32 KiB backing is the right memory for every VPU workload.
"""

from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from pathlib import Path
from typing import NoReturn


BUILD_TAG = b"5c6c275"
VPU_CONFIG_OFFSET = 0x343
VPU_CONFIG_OLD = bytes.fromhex("41 c7 46 38 00 00 00 20")
VPU_CONFIG_NEW = bytes.fromhex("41 c7 46 38 00 00 00 10")


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"Guest VPU heap-group patch refused: {message}")


def load_heap_count_helpers():
    helper_path = Path(__file__).with_name("patch-guest-physheap-count.py")
    spec = importlib.util.spec_from_file_location("guest_physheap_count_patch", helper_path)
    if spec is None or spec.loader is None:
        fail("cannot load the ELF section helpers")
    helper = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = helper
    spec.loader.exec_module(helper)
    return helper


def patch_body(body: bytes) -> bytes:
    end = VPU_CONFIG_OFFSET + len(VPU_CONFIG_OLD)
    if len(body) < end:
        raise ValueError("SysDevInit body is shorter than the guarded Guest VPU config")
    actual = body[VPU_CONFIG_OFFSET:end]
    if actual != VPU_CONFIG_OLD:
        raise ValueError(f"Guest VPU heap config changed: {actual.hex()}")
    patched = bytearray(body)
    patched[VPU_CONFIG_OFFSET:end] = VPU_CONFIG_NEW
    return bytes(patched)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, help="linked mtgpu.ko to patch in-place")
    args = parser.parse_args()
    path = args.module
    data = path.read_bytes()
    if BUILD_TAG not in data:
        fail("module does not contain the expected 5c6c275 core build tag")

    helper = load_heap_count_helpers()
    sections, named = helper.section_table(data)
    symbol_value, text_addr = helper.find_sysdevinit(data, sections, named)
    _text_index, text = named[".text"]
    if symbol_value < text_addr:
        fail("SysDevInit symbol is outside .text")
    body_file_offset = text[4] + symbol_value - text_addr
    end = VPU_CONFIG_OFFSET + len(VPU_CONFIG_OLD)
    body = data[body_file_offset:body_file_offset + end]
    try:
        patched = patch_body(body)
    except ValueError as exc:
        fail(str(exc))

    file_offset = body_file_offset + VPU_CONFIG_OFFSET
    with path.open("r+b") as module:
        module.seek(file_offset)
        module.write(VPU_CONFIG_NEW)
        module.flush()

    result = path.read_bytes()
    if result[file_offset:file_offset + len(VPU_CONFIG_NEW)] != VPU_CONFIG_NEW:
        fail("post-write verification failed")
    if result[:file_offset] != data[:file_offset] or result[file_offset + len(VPU_CONFIG_NEW):] != data[file_offset + len(VPU_CONFIG_NEW):]:
        fail("unexpected bytes changed outside the Guest VPU config immediate")
    print(f"patched {path}: Guest VPU usage flag GROUP2 (0x20000000) -> GROUP1 (0x10000000)")
    print("Only SysDevInit+0x347..+0x34a changed; config count and all addresses are unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
