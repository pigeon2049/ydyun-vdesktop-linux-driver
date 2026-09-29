#!/usr/bin/env python3
"""Decode a saved Linux vGPU info-page v1 snapshot; never access hardware."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


MAGIC = 0xAA557491
VERSION = 1
DEVICE_TYPES = (("vgpu", 0x28), ("vvpu", 0x438))
MAX_SEGMENTS = 64
SEGMENT_INFO_SIZE = 0x410
INFO_PAGE_SIZE = 0x8A0
FLAG_NAMES = {
    0: "enable_vpu",
    1: "enable_iommu_dma",
    2: "enable_iommu_irq",
    3: "vpu_bar4",
    4: "pb_shared",
}


def decode(data: bytes, segment_stride: int = 16) -> dict:
    if segment_stride not in (16, 24):
        raise ValueError('Segment stride must be 16 or 24')
    tail_delta = 2 * MAX_SEGMENTS * (segment_stride - 16)
    required_size = INFO_PAGE_SIZE + tail_delta
    if len(data) < required_size:
        raise ValueError(f"Truncated Linux v1 info page (need 0x{required_size:x} bytes)")

    def u32(offset: int) -> int:
        return struct.unpack_from("<I", data, offset)[0]

    def u64(offset: int) -> int:
        return struct.unpack_from("<Q", data, offset)[0]

    def tail32(offset: int) -> int:
        return u32(offset + tail_delta)

    def tail64(offset: int) -> int:
        return u64(offset + tail_delta)

    magic, version, osid = u32(0), u32(4), u32(8)
    if magic != MAGIC:
        raise ValueError(f"Unexpected info-page magic {magic:#010x}")
    if version != VERSION:
        raise ValueError(f"Expected Linux info-page v1, got version {version}")

    flags = u64(0x10)
    result = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "layout_source": ("official Linux vGPU 2.3.0 struct vgpu_info (version 1)"
                          if segment_stride == 16 else
                          "observed Linux v1 response with 24-byte typed segment records (r16b)"),
        "segment_stride_bytes": segment_stride,
        "input_size_bytes": len(data),
        "version": version,
        "osid": osid,
        "flags": hex(flags),
        "flag_bits": {name: bool(flags & (1 << bit)) for bit, name in FLAG_NAMES.items()},
        "vm_memory_size_bytes": u64(0x18),
        "vm_bar2_actual_memory_size_bytes": u64(0x20),
        "segments": {},
        "fw_heap_base_raw": hex(tail64(0x848)),
        "fw_heap_size_bytes": tail32(0x850),
        "mmu_heap_base_raw": hex(tail64(0x858)),
        "mmu_heap_size_bytes": tail32(0x860),
        "extension_size_bytes": tail64(0x868),
        "extension": {
            "max_resolution_width": tail32(0x878),
            "max_resolution_height": tail32(0x87C),
            "max_encode_instances": tail32(0x880),
            "max_decode_instances": tail32(0x884),
            "pb_free_list_device_address_raw": hex(tail64(0x888)),
            "pb_free_list_physical_address_raw": hex(tail64(0x890)),
            "pb_free_list_size_bytes": tail32(0x898),
            "mpc_core_count": tail32(0x89C),
        },
        "address_domain_note": (
            "Base fields are decoded as published. This tool does not infer a BAR2 offset, "
            "Guest-to-Host translation, or physical backing."
        ),
    }

    for group_index, (name, old_start) in enumerate(DEVICE_TYPES):
        start = old_start + group_index * MAX_SEGMENTS * (segment_stride - 16)
        size = u64(start)
        count = u32(start + 8)
        if count > MAX_SEGMENTS:
            raise ValueError(f"{name} segment count {count} exceeds {MAX_SEGMENTS}")
        records = []
        for index in range(count):
            offset = start + 0x10 + index * segment_stride
            base, length = struct.unpack_from("<QQ", data, offset)
            end = base + length
            if end > (1 << 64):
                raise ValueError(f"{name} segment {index} wraps the uint64 address space")
            records.append({
                "index": index,
                "base_raw": hex(base),
                "size_bytes": length,
                "end_exclusive_raw": hex(end),
            })
            if segment_stride == 24:
                records[-1]['flags_raw'] = hex(u64(offset + 16))
        if segment_stride == 24 and sum(r['size_bytes'] for r in records) != size:
            raise ValueError(f'{name} aggregate size disagrees with typed segment records')
        result["segments"][name] = {
            "aggregate_size_bytes": size,
            "count": count,
            "records": records,
        }
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="saved raw Linux version-1 info page")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--segment-stride", type=int, choices=(16, 24), default=16,
                        help="explicit ABI selection; version 1 alone is insufficient")
    args = parser.parse_args()
    try:
        result = decode(args.input.read_bytes(), args.segment_stride)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
