#!/usr/bin/env python3
"""Map Linux QY1 3D packet offsets back to their UMD descriptor inputs.

Runs the original libsrv_um_MUSA.so construction through the bounded RAM
oracle once per probe word, changing exactly one source field and diffing the
generated packet. The result is a data-only dependency table: it records which
packet bytes a descriptor field can reach, and at which bit width. No value is
interpreted as a hardware command and nothing is submitted to a device.
"""
import json
import struct
from pathlib import Path

from linux_gfx_reference import LinuxGfxOracle

ROOT = Path(__file__).resolve().parents[1]

# (name, oracle struct size, oracle state() parameter index)
DESCRIPTORS = [
    ("job", 0x7b8, 0),
    ("surface", 0x600, 1),
    ("render", 0x200, 2),
    ("vertex", 0x140, 3),
    ("context", 0x330, 4),
]

# Values chosen so a single probe cannot be confused with a neighbouring field.
PROBE32 = 0x5A3C69F1
PROBE64 = 0xC3A5F10D7B296E41


def baseline():
    return LinuxGfxOracle().packet(*[bytes(n) for _, n, _ in DESCRIPTORS])["packet"]


def probe(descriptor, offset, payload):
    fields = [bytes(n) for _, n, _ in DESCRIPTORS]
    buf = bytearray(fields[descriptor])
    buf[offset:offset + len(payload)] = payload
    fields[descriptor] = bytes(buf)
    return LinuxGfxOracle().packet(*fields)["packet"]


def byte_spans(before, after):
    changed = [i for i in range(min(len(before), len(after))) if before[i] != after[i]]
    if not changed:
        return []
    spans = []
    start = prev = changed[0]
    for i in changed[1:]:
        if i == prev + 1:
            prev = i
            continue
        spans.append((start, prev))
        start = prev = i
    spans.append((start, prev))
    return spans


def main():
    import sys

    only = set(sys.argv[1:]) or {name for name, _, _ in DESCRIPTORS}
    unknown = only - {name for name, _, _ in DESCRIPTORS}
    if unknown:
        raise SystemExit(f"unknown descriptor(s): {' '.join(sorted(unknown))}")

    base = baseline()
    table = []
    for name, size, index in DESCRIPTORS:
        if name not in only:
            continue
        for offset in range(0, size - 3, 4):
            spans = byte_spans(base, probe(index, offset, struct.pack("<I", PROBE32)))
            if not spans:
                continue
            table.append({
                "descriptor": name,
                "parameter": index,
                "source_offset": offset,
                "packet_spans": [[hex(a), hex(b)] for a, b in spans],
                "packet_offset": hex(spans[0][0]),
            })

    result = {
        "packet_bytes": len(base),
        "descriptors": {n: {"bytes": s, "parameter": i} for n, s, i in DESCRIPTORS},
        "probe_dword": hex(PROBE32),
        "probed": sorted(only),
        "reachable_words": len(table),
        "fields": table,
    }
    out = ROOT / "reports/packet-field-map.json"
    out.write_text(json.dumps(result, indent=2) + "\n")
    for row in table:
        spans = ",".join(f"{a}-{b}" for a, b in row["packet_spans"])
        print(f"{row['descriptor']:<8} +{row['source_offset']:#05x} -> packet {spans}")
    print(f"\n{len(table)} reachable words, wrote {out}")


if __name__ == "__main__":
    main()
