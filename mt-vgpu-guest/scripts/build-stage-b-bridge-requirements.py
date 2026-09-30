#!/usr/bin/env python3
"""Build the machine-readable Stage B bridge requirement table.

The Stage B kernel bridge has to answer exactly the line formats the legacy
MUSA user-mode driver speaks. This script joins three sources of truth:

1. 5.2.0 KMD generated headers (the generation the UMD was built against),
   kept in ``reference/kmd-5.2.0-server-generated/``; 2.7.1 generated headers
   under ``src/`` cover the RGX bridge groups the Host package omits.
2. ``reports/legacy-umd-pvr-bridge-abi.json`` -- the 205 bridge callsites the
   UMD actually issues, with the in/out sizes the driver passes.
3. Bridge traces produced by ``probe/umd_bridge_shim.so`` -- which commands a
   real connect -> devmem -> render context -> sync session exercises, and the
   exact sizes seen on the wire.

Output: ``reports/stage-b-bridge-requirements.json``.

Nothing here talks to hardware; it only reads repository text.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KMD52 = ROOT / "reference" / "kmd-5.2.0-server-generated"
KMD271 = ROOT / "src" / "mtgpu-2.7.1-6.12" / "inc" / "pvr" / "generated"
KMD271_SVC = ROOT / "src" / "mtgpu-2.7.1-6.12" / "inc" / "pvr" / "services"
ABI_JSON = ROOT / "reports" / "legacy-umd-pvr-bridge-abi.json"
OUT_JSON = ROOT / "reports" / "stage-b-bridge-requirements.json"

DEFAULT_TRACES = [
    ROOT / "reports" / "umd-bridge-connect-trace.jsonl",
    ROOT / "reports" / "umd-bridge-devmem-trace.jsonl",
    ROOT / "reports" / "umd-bridge-renderctx-trace.jsonl",
    ROOT / "reports" / "umd-bridge-sync-trace.jsonl",
]

# Bridge group ids. 5.2 Host headers define 0..26; the RGX/MUSA groups start at
# 128 and are only declared by the 2.7.1 tree.
GROUPS_5_2 = {
    0: "DEFAULT", 1: "SRVCORE", 2: "SYNC", 3: "RESERVED1", 4: "RESERVED2",
    5: "PDUMPCTRL", 6: "MM", 7: "MMPLAT", 8: "CMM", 9: "PDUMPMM", 10: "PDUMP",
    11: "DMABUF", 12: "DC", 13: "CACHE", 14: "SMM", 15: "MTTL", 16: "RI",
    17: "VALIDATION", 18: "TUTILS", 19: "DEVICEMEMHISTORY", 20: "HTBUFFER",
    21: "DCPLAT", 22: "MMEXTMEM", 23: "SYNCTRACKING", 24: "SYNCFALLBACK",
    25: "DI", 26: "DMA",
}
GROUPS_27 = {
    128: "RGXTQ", 129: "RGXCMP", 130: "RGXTA3D", 131: "RGXBREAKPOINT",
    132: "RGXFWDBG", 133: "RGXPDUMP", 134: "RGXHWPERF", 135: "RGXREGCONFIG",
    136: "RGXKICKSYNC", 137: "RGXTQ2", 138: "RGXTIMERQUERY", 139: "RGXDMA",
    140: "MUSACE",
}

# Packed scalar widths, verified against img_types.h / mem_types.h of the 5.2
# package. Unknown types are rejected rather than guessed: the caller decides.
SCALARS = {
    "MT_CHAR": 1, "char": 1, "int8_t": 1, "uint8_t": 1, "u8": 1,
    "MT_BYTE": 1, "MT_UINT8": 1,
    "MT_INT16": 2, "MT_UINT16": 2, "uint16_t": 2, "u16": 2,
    "MT_BOOL": 4, "MT_INT": 4, "MT_INT32": 4, "MT_UINT32": 4, "uint32_t": 4,
    "u32": 4, "MT_FLOAT": 4, "float": 4, "MTGPU_ERROR": 4, "MT_PID": 4,
    "MT_UINT": 4, "MTGPU_MEMALLOCFLAGS_T": 4, "PDUMP_FLAGS_T": 4,
    "MT_INT64": 8, "int64_t": 8, "i64": 8, "MT_UINT64": 8, "uint64_t": 8,
    "u64": 8, "MT_HANDLE": 8, "MTGPU_FENCE": 8, "MTGPU_TIMELINE": 8,
    "MT_DEV_VIRTADDR": 8, "MT_DEV_PHYADDR": 8, "MT_GPUADDR": 8,
    "MT_CPU_PHYADDR": 8, "MT_DEVMEM_SIZE_T": 8, "MT_DMA_ADDR": 8,
    "MT_DEVMEM_OFFSET_T": 8, "MT_DEVMEM_ALIGN_T": 8, "MT_SIZE_T": 8,
    "size_t": 8, "void": 8,
    # The 2.7.1 tree spells the same widths with the legacy IMG_/PVRSRV_ names.
    "IMG_CHAR": 1, "IMG_BYTE": 1, "IMG_UINT8": 1,
    "IMG_INT16": 2, "IMG_UINT16": 2,
    "IMG_BOOL": 4, "IMG_INT": 4, "IMG_INT32": 4, "IMG_UINT32": 4,
    "IMG_FLOAT": 4, "IMG_PID": 4,
    "IMG_INT64": 8, "IMG_UINT64": 8, "IMG_HANDLE": 8, "IMG_FENCE": 8,
    "IMG_TIMELINE": 8, "IMG_DEV_VIRTADDR": 8, "IMG_DEV_PHYADDR": 8,
    "IMG_GPUADDR": 8, "IMG_DEVMEM_SIZE_T": 8, "IMG_DEVMEM_OFFSET_T": 8,
    "IMG_DEVMEM_ALIGN_T": 8, "IMG_CPU_PHYADDR": 8, "IMG_DMA_ADDR": 8,
    "PVRSRV_ERROR": 4, "MTGPU_ERROR_TAG": 4,
}

STRUCT_RE = re.compile(
    r"typedef\s+struct\s+(?P<tag>\w+)_TAG\s*\{(?P<body>[^}]*)\}\s*__packed\s+"
    r"(?P<prefix>\w+)\_(?P<dir>IN|OUT)_(?P<name>\w+)\s*;",
    re.S,
)


def parse_bridge_ids(path, pattern):
    """Read '#define MTGPU_BRIDGE_<NAME> <n>UL' style id tables."""
    ids = {}
    if not path.is_file():
        return ids
    text = path.read_text(errors="replace")
    for match in re.finditer(pattern, text):
        ids[match.group(1)] = int(match.group(2))
    return ids


def parse_commands(headers, macro_prefix, groups):
    """Map (bridge_id, function_id) -> command macro name and header."""
    result = {}
    for header in sorted(headers.glob("common_*_bridge.h")):
        text = header.read_text(errors="replace")
        for line in text.splitlines():
            match = re.match(r"\s*#define\s+(" + re.escape(macro_prefix) +
                             r"\w+?)_(\w+)\s+\(?\s*(\w+)_CMD_FIRST\s*\+\s*(\d+)"
                             r"\s*\)?\s*$", line)
            if not match:
                continue
            group_macro, name, group_macro2, offset = match.groups()
            if group_macro2 != group_macro:
                continue
            group_name = group_macro[len(macro_prefix):]
            group_id = next((gid for gid, gname in groups.items()
                             if gname == group_name), None)
            if group_id is None:
                continue
            result[(group_id, int(offset))] = {
                "name": name,
                "group": group_name,
                "header": str(header.relative_to(ROOT)),
            }
    return result


def field_width(tokens):
    """Return (name, type, width) for one packed struct field, or None.

    Declarations in these headers come as ``MT_CHAR *name`` or
    ``const MT_CHAR *name``, so the pointer marker can sit on either token.
    """
    if len(tokens) < 2:
        return None
    name = tokens[-1].lstrip("*")
    types = [t for t in tokens[:-1]
             if t not in {"const", "struct", "volatile", "unsigned", "signed"}]
    if not name or name in {"struct", "union"}:
        return None
    if any("*" in token for token in tokens):
        return name, " ".join(types) + " *", 8
    if not types:
        return None
    ctype = types[-1]
    width = SCALARS.get(ctype)
    if width is None:
        return None
    return name, ctype, width


def parse_structs(headers):
    """Map (dir, struct name) -> ordered fields with packed byte offsets."""
    structs = {}
    for header in sorted(headers.glob("*.h")):
        text = header.read_text(errors="replace")
        for match in STRUCT_RE.finditer(text):
            body = match.group("body")
            fields = []
            offset = 0
            unknown = []
            for raw in body.split(";"):
                line = re.sub(r"/\*.*?\*/", "", raw).strip()
                if not line or line.startswith("//"):
                    continue
                parsed = field_width(re.findall(r"[A-Za-z_]\w*|\*", line))
                if parsed is None:
                    unknown.append(" ".join(line.split()))
                    continue
                name, ctype, width = parsed
                fields.append({"name": name, "type": ctype, "offset": offset,
                               "size": width})
                offset += width
            structs[(match.group("dir"), match.group("name"))] = {
                "tag": match.group("tag"),
                "size": offset,
                "fields": fields,
                "unparsed": unknown,
                "header": str(header.relative_to(ROOT)),
            }
    return structs


def load_umd_callsites():
    if not ABI_JSON.is_file():
        return {}
    data = json.loads(ABI_JSON.read_text())
    result = {}
    for call in data.get("callsites", []):
        result[(call["bridge_id"], call["function_id"])] = {
            "call_offset": call.get("call_offset"),
            "input_size": call.get("input_size"),
            "output_size": call.get("output_size"),
            "bridge": call.get("bridge"),
            "kmd_2_3_function": call.get("kmd_2_3_function"),
        }
    return result


def load_observed(traces):
    """Count command firings and the exact wire sizes seen per command."""
    observed = {}
    for path in traces:
        if not Path(path).is_file():
            continue
        for line in Path(path).read_text(errors="replace").splitlines():
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            if entry.get("op") != "ioctl" or not entry.get("bridge"):
                continue
            text = entry["bridge"]
            try:
                bridge_id = int(text.split(":")[0], 16)
                function_id = int(text.split(":")[1], 16)
            except (IndexError, ValueError):
                continue
            key = (bridge_id, function_id)
            record = observed.setdefault(key, {
                "count": 0, "input_sizes": set(), "output_sizes": set(),
                "traces": set(),
            })
            record["count"] += 1
            if entry.get("in_size") is not None:
                record["input_sizes"].add(entry["in_size"])
            if entry.get("out_size") is not None:
                record["output_sizes"].add(entry["out_size"])
            record["traces"].add(str(Path(path).name))
    return observed


def main():
    traces = [Path(a) for a in sys.argv[1:]] or DEFAULT_TRACES

    groups = dict(GROUPS_5_2)
    groups.update(GROUPS_27)
    commands = {}
    commands.update(parse_commands(KMD52, "MTGPU_BRIDGE_", GROUPS_5_2))
    commands.update(parse_commands(KMD271, "PVRSRV_BRIDGE_", GROUPS_27))
    structs = parse_structs(KMD52)
    # The 5.2 package is authoritative for this UMD, so its layouts win; the
    # 2.7.1 tree only fills in names the Host package does not declare.
    for key, value in parse_structs(KMD271).items():
        structs.setdefault(key, value)
    callsites = load_umd_callsites()
    observed = load_observed(traces)

    entries = []
    for key in sorted(set(observed) | set(callsites)):
        bridge_id, function_id = key
        command = commands.get(key, {})
        umd = callsites.get(key, {})
        seen = observed.get(key)
        entry = {
            "bridge_id": bridge_id,
            "function_id": function_id,
            "bridge": "0x%x" % bridge_id,
            "function": "0x%x" % function_id,
            "group": command.get("group") or GROUPS_5_2.get(bridge_id)
                      or GROUPS_27.get(bridge_id),
            "command": command.get("name"),
            "header": command.get("header"),
            "umd_input_size": umd.get("input_size"),
            "umd_output_size": umd.get("output_size"),
            "umd_callsite": umd.get("call_offset"),
        }
        if seen:
            entry["observed"] = {
                "count": seen["count"],
                "input_sizes": sorted(seen["input_sizes"]),
                "output_sizes": sorted(seen["output_sizes"]),
                "traces": sorted(seen["traces"]),
            }
        for direction, size_key in (("IN", "umd_input_size"),
                                    ("OUT", "umd_output_size")):
            suffix = command.get("name")
            if not suffix:
                continue
            struct = structs.get((direction, suffix))
            if struct is None:
                continue
            entry[f"{direction.lower()}_struct"] = {
                "tag": struct["tag"],
                "header": struct["header"],
                "size": struct["size"],
                "fields": struct["fields"],
                "unparsed_lines": struct["unparsed"],
            }
            size = entry[direction.lower() + "_struct"]["size"]
            declared = entry[size_key]
            if declared is not None and size != declared:
                entry.setdefault("size_disagreements", []).append(
                    {"direction": direction, "header_size": size,
                     "umd_declared": declared})
        entries.append(entry)

    observed_keys = sorted(observed)
    document = {
        "purpose": "Stage B kernel bridge line-format requirements",
        "sources": {
            "kmd_5_2_generated": str(KMD52.relative_to(ROOT)),
            "kmd_2_7_1_generated": str(KMD271.relative_to(ROOT)),
            "umd_bridge_audit": str(ABI_JSON.relative_to(ROOT)),
            "traces": [str(Path(t)) for t in traces],
        },
        "type_widths_verified_against": "5.2.0 inc/mt/include/img_types.h",
        "counts": {
            "commands_parsed_from_headers": len(commands),
            "umd_callsites": len(callsites),
            "commands_observed_in_traces": len(observed_keys),
            "commands_required_by_observed_session": len(observed_keys),
        },
        "required_now": [
            {"bridge": e["bridge"], "function": e["function"],
             "command": e["command"], "group": e["group"],
             "in": e.get("umd_input_size"),
             "out": e.get("umd_output_size"),
             "observed": e.get("observed", {}).get("count")}
            for e in entries if "observed" in e
        ],
        "entries": entries,
    }
    OUT_JSON.write_text(json.dumps(document, indent=1, sort_keys=False) + "\n")
    print("wrote %s: %d entries, %d observed"
          % (OUT_JSON.relative_to(ROOT), len(entries), len(observed_keys)))
    disagreements = [e for e in entries if "size_disagreements" in e]
    for entry in disagreements:
        print("  size disagreement: 0x%x:0x%x %s %s"
              % (entry["bridge_id"], entry["function_id"], entry["command"],
                 entry["size_disagreements"]))


if __name__ == "__main__":
    main()
