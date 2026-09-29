#!/usr/bin/env python3
"""Extract embedded firmware and decode loader records; do not load hardware."""
import hashlib
import json
from pathlib import Path
import struct
from pe_image import ReferencePE

ROOT = Path(__file__).resolve().parents[1]
IMAGES = [
    ("gen1", 0x141031160, 0x141058560),
    ("gen2", 0x141058660, 0x141085370),
    ("gen2_1", 0x141085470, 0x1410AE680),
    ("gen3", 0x1410AE780, 0x1410DCF40),
    ("gen6", 0x1410DD040, 0x1411062A0),
]


def parse_loader(blob, segments):
    def read(offset, size):
        if offset < 0 or size < 0 or offset + size > len(blob):
            raise ValueError(f"Loader range out of bounds: {offset:#x}+{size:#x}")
        return blob[offset:offset + size]

    def translated(address, size, zero=False):
        # 14001f0b0 distinguishes on-chip windows from translated segments.
        for base in (0x80000000, 0x82000000):
            if base <= address < base + 0x18000:
                if address + size > base + 0x18000:
                    raise ValueError("On-chip load exceeds bootstrap window")
                if zero and base == 0x82000000:
                    return {"region": "skipped_zero", "offset": hex(address - base)}
                if not zero:
                    return {"region": "bootstrap", "offset": hex(0x29160 + address - base)}
        normalized = address & 0x7fffffff
        for entry in segments:
            if entry[2] <= normalized < entry[2] + entry[4]:
                if normalized + size > entry[2] + entry[4]:
                    raise ValueError("Load exceeds firmware segment")
                return {"region": "firmware", "offset": hex(normalized - entry[2] + entry[5])}
        raise ValueError(f"No translation for firmware address {address:#x}")

    cursor = struct.unpack("<I", read(8, 4))[0]
    seen = set()
    records = []
    while cursor != 0xffffffff:
        if cursor in seen:
            raise ValueError("Cyclic loader command list")
        seen.add(cursor)
        command, _, next_offset, first, second = struct.unpack("<HHIII", read(cursor, 16))
        record = {"record_offset": hex(cursor), "command": hex(command)}
        kind = command & 15
        if command & 0x10:
            record["action"] = "skip"
        elif kind == 0:
            size = struct.unpack("<H", read(second + 2, 2))[0] - 6
            payload = read(second + 4, size)
            record.update(action="copy", source_offset=hex(second + 4), address=hex(first),
                          size=size, sha256=hashlib.sha256(payload).hexdigest(),
                          destination=translated(first, size))
        elif kind == 4:
            record.update(action="zero", address=hex(first), size=second,
                          destination=translated(first, second, zero=True))
        elif kind == 5:
            size = struct.unpack("<H", read(first + 2, 2))[0] - 6
            if size < 0 or size % 12:
                raise ValueError("Invalid register-script length")
            instructions = []
            for offset in range(first + 4, first + 4 + size, 12):
                op, address, value = struct.unpack("<III", read(offset, 12))
                if op != 2:
                    raise ValueError("Unsupported register-script opcode")
                instructions.append({"address": hex(address), "value": hex(value)})
            record.update(action="register_script", instructions=instructions)
        elif kind == 3:
            record["action"] = "no_copy"
        else:
            raise ValueError(f"Unsupported loader command {command:#x}")
        records.append(record)
        cursor = next_offset
    return records


def build_guest_loader_image(blob, segments, firmware_va, mmu_mode=0):
    """Reproduce 14001eee0's loader stage in private RAM, before 140015f78.

    Guest code/state share the same allocation base (14001728c/140015dc8).
    This output lacks the state/resources written later and must not be uploaded.
    """
    if (firmware_va < 0 or firmware_va & 4095 or
            firmware_va > (1 << 40) - 0x800000 or mmu_mode not in (0, 1)):
        raise ValueError("Invalid firmware VA or MMU mode")
    records = parse_loader(blob, segments)
    result = bytearray(0x800000)

    def write(offset, data):
        if offset < 0 or offset + len(data) > len(result):
            raise ValueError("Loader destination exceeds Guest allocation")
        result[offset:offset + len(data)] = data

    write(0x200, struct.pack('<II', 0x4830030, 4))
    cursor = 0x208
    for entry in segments:
        index, kind, base, _, length, offset = entry
        if kind == 2:
            address = (((1 << 48) if mmu_mode else (7 << 40)) | firmware_va) + offset
            words = [(index + 0x485000) * 16, base | 0xf02,
                     index * 16 + 0x4850004, (length - 4096) & 0xffffffff,
                     index * 16 + 0x4850008, address & 0xffffffff,
                     index * 16 + 0x485000c, address >> 32]
            write(cursor, struct.pack('<8I', *words))
            cursor += 32
    for record in records:
        action = record['action']
        if action in ('copy', 'zero'):
            dest = record['destination']
            if dest['region'] == 'skipped_zero':
                continue
            offset = int(dest['offset'], 16)
            size = record['size']
            if action == 'copy':
                source = int(record['source_offset'], 16)
                payload = blob[source:source + size]
            else:
                if size > len(result):
                    raise ValueError("Zero record exceeds Guest allocation")
                payload = bytes(size)
            write(offset, payload)
        elif action == 'register_script':
            for instruction in record['instructions']:
                write(cursor, struct.pack('<II', int(instruction['address'], 16),
                                           int(instruction['value'], 16)))
                cursor += 8
    write(cursor, struct.pack('<III', 0, 0, 0x90000000))
    return bytes(result)


def main():
    pe = ReferencePE("/opt/MTT-driver-only/mtkm64.sys")
    destination = ROOT / "reference/firmware"
    destination.mkdir(parents=True, exist_ok=True)
    summary = {"reference_sha256": pe.sha256, "loader_function": "14001f0b0",
               "uploaded": False, "selected_for_hardware": None, "images": []}
    for index, (name, start, metadata) in enumerate(IMAGES):
        version, header, count, stride = struct.unpack("<4I", pe.read(metadata, 16))
        if version != 1 or header != 16 or count > 8 or stride != 24:
            raise ValueError("Unexpected firmware metadata header")
        segments = [struct.unpack("<6I", pe.read(metadata + header + i * stride, stride))
                    for i in range(count)]
        blob = pe.read(start, metadata - start)
        records = parse_loader(blob, segments)
        entry = {"name": name, "selector": index, "va": hex(start), "size": len(blob),
                 "sha256": hashlib.sha256(blob).hexdigest(), "segment_count": count,
                 "record_count": len(records),
                 "copied_bytes": sum(r.get("size", 0) for r in records if r["action"] == "copy")}
        summary["images"].append(entry)
        (destination / f"{name}.ldr").write_bytes(blob)
        (destination / f"{name}.json").write_text(json.dumps({**entry,
            "segments": [list(map(hex, s)) for s in segments], "records": records}, indent=2) + "\n")
    (ROOT / "reports/firmware-image-inventory.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
