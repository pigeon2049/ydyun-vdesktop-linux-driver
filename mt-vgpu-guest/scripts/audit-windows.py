#!/usr/bin/env python3
"""Extract INF IDs, hashes and PE version/build evidence without executing binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def pe_metadata(data):
    if len(data) < 64 or data[:2] != b"MZ":
        return None
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        return None
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    # VS_FIXEDFILEINFO signature: retain all candidates, not just the first match.
    versions = []
    for match in re.finditer(re.escape(b"\xbd\x04\xef\xfe"), data):
        if match.start() + 24 > len(data):
            continue
        sig, ver, ms, ls, pms, pls = struct.unpack_from("<6I", data, match.start())
        if ver != 0x10000:
            continue
        fmt = lambda a, b: ".".join(map(str, (a >> 16, a & 65535, b >> 16, b & 65535)))
        versions.append({"file_version": fmt(ms, ls), "product_version": fmt(pms, pls)})
    strings = [x.decode("ascii") for x in re.findall(rb"[\x20-\x7e]{6,}", data)]
    clues = sorted(set(s for s in strings if re.search(r"guest\d+|\.pdb$|VGPU Env|VGPUFWStatusCheck", s, re.I)))
    return {"machine": hex(machine), "versions": versions, "build_clues": clues}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path, nargs="?", default=Path("/opt/MTT-driver-only"))
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if not args.directory.is_dir():
        p.error("Reference driver directory does not exist")
    result = {"reference_directory": str(args.directory.resolve()), "files": []}
    for path in sorted(args.directory.iterdir()):
        if not path.is_file():
            continue
        data = path.read_bytes()
        item = {"name": path.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        if path.suffix.lower() == ".inf":
            text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig", errors="replace")
            item["driver_versions"] = re.findall(r"^DriverVer\s*=\s*(.*)$", text, re.M | re.I)
            item["matching_device_lines"] = [line for line in text.splitlines() if "0222.1101" in line]
            item["linux_loadable"] = False
        elif path.suffix.lower() in (".sys", ".dll"):
            item["pe"] = pe_metadata(data)
            item["linux_loadable"] = False
        result["files"].append(item)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
