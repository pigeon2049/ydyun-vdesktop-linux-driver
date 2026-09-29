#!/usr/bin/env python3
"""Decode the observed version-2 vGPU response; never access hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def decode(data):
    if len(data) < 0xCC8:
        raise ValueError("Truncated v2 information response (need 0xcc8 bytes)")
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    u64 = lambda offset: struct.unpack_from("<Q", data, offset)[0]
    if u32(0) != 0xAA557491:
        raise ValueError("Unrecognized information signature")
    if u32(4) != 2:
        raise ValueError("Only the measured version-2 layout is supported")
    count = u32(0xC50)
    if count > (0xC48 - 0x28) // 24:
        raise ValueError("Segment table overlaps the request capability field")
    result = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "layout_source": "mtkm64.sys 30.0.2505.1, hardware-validated version 2",
        "version": u32(4), "osid": u32(8), "flags": hex(u64(0x10)),
        "vm_memory_size_bytes": u64(0x18),
        "bar2_actual_memory_size_bytes": u64(0x20),
        "segment_count": count, "segments": [],
        # Input bits set by the Guest: do not claim they are Host capabilities.
        "request_capability_bits": hex(u64(0xC48)),
        "extension_size_bytes": u64(0xC68),
        "max_width": u32(0xC78), "max_height": u32(0xC7C),
        "max_encode_instances": u32(0xC80), "max_decode_instances": u32(0xC84),
        "pb_free_list_device_address": hex(u64(0xC88)),
        "pb_free_list_physical_address": hex(u64(0xC90)),
        "pb_free_list_size_bytes": u32(0xC98), "mpc_core_count": u32(0xC9C),
        "raw_extension_words": {hex(o): hex(u64(o)) for o in range(0xCA0, 0xCC8, 8)},
        "hardware_acceleration_verified": False,
    }
    # FUN_140027ab4: bit 0x10 adds the PB free-list range before segments
    # carrying any of bits 0..2. Device addresses are not BAR2 offsets.
    cursor = u32(0xC98) if u64(0x10) & 0x10 else 0
    for i in range(count):
        offset = 0x28 + i * 24
        base, size, flags = struct.unpack_from("<QQQ", data, offset)
        if base + size > (1 << 64):
            raise ValueError("Segment address range wraps uint64")
        segment = {"index": i, "address": hex(base), "size_bytes": size, "flags": hex(flags)}
        if flags & 7:
            if cursor + size > u64(0x20):
                raise ValueError("Mapped segment exceeds the reported BAR2 memory size")
            segment["bar2_offset"] = hex(cursor)
            cursor += size
        if flags & 0x20 and u64(0x10) & 0x80:
            segment["shared_bar2_offset"] = hex(base)
        result["segments"].append(segment)
    result["mapped_segment_end"] = hex(cursor)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = decode(args.input.read_bytes())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
