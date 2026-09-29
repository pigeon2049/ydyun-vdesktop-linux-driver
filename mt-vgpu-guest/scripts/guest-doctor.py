#!/usr/bin/env python3
"""Read-only PCI/DRM/rendering diagnosis. Does not read MMIO or load drivers."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def run(argv):
    executable = shutil.which(argv[0]) or shutil.which(argv[0], path="/usr/sbin:/sbin")
    if not executable:
        return {"command": argv, "returncode": None, "output": "command unavailable"}
    try:
        p = subprocess.run([executable, *argv[1:]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace", timeout=20)
        return {"command": argv, "returncode": p.returncode, "output": p.stdout.strip()}
    except subprocess.TimeoutExpired:
        return {"command": argv, "returncode": None, "output": "timeout after 20s"}


def pci_devices(base=Path("/sys/bus/pci/devices")):
    result = []
    for p in sorted(base.glob("*")):
        if read(p / "vendor") != "0x1ed5":
            continue
        item = {k: read(p / k) for k in ("vendor", "device", "subsystem_vendor", "subsystem_device", "class", "modalias")}
        item["bdf"] = p.name
        item["driver"] = (p / "driver").resolve().name if (p / "driver").exists() else None
        item["resources"] = read(p / "resource")
        item["drm_nodes"] = sorted(x.name for x in (p / "drm").glob("*")
                                   if re.fullmatch(r"card\d+|renderD\d+", x.name))
        result.append(item)
    return result


def drm_nodes(base=Path("/sys/class/drm")):
    nodes = []
    for p in sorted(base.glob("*")):
        if not re.fullmatch(r"card\d+|renderD\d+", p.name):
            continue
        dev = (p / "device").resolve()
        nodes.append({"node": "/dev/dri/" + p.name, "device": dev.name,
                      "vendor": read(dev / "vendor"),
                      "driver": (dev / "driver").resolve().name if (dev / "driver").exists() else None})
    return nodes


def module_build_id(note_path):
    """Decode the kernel's exported ELF GNU build-id note without reading code."""
    try:
        data = Path(note_path).read_bytes()
    except OSError:
        return None
    if len(data) < 16:
        return None
    order = "<" if sys.byteorder == "little" else ">"
    namesz, descsz, note_type = struct.unpack_from(order + "III", data)
    name_start = 12
    desc_start = name_start + ((namesz + 3) & ~3)
    if (namesz != 4 or descsz < 4 or descsz > 64 or note_type != 3 or
            desc_start + descsz > len(data) or data[name_start:name_start + 4] != b"GNU\0"):
        return None
    return data[desc_start:desc_start + descsz].hex()


def module_state(name="mtgpu", base=Path("/sys/module")):
    path = Path(base) / name
    if not path.is_dir():
        return {"present": False}
    return {
        "present": True,
        "initstate": read(path / "initstate"),
        "refcnt": read(path / "refcnt"),
        "taint": read(path / "taint"),
        "core_size": read(path / "coresize"),
        "init_size": read(path / "initsize"),
        "build_id": module_build_id(path / "notes/.note.gnu.build-id"),
    }


def candidate_state(path):
    if path is None:
        return None
    path = Path(path).resolve()
    try:
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
    except OSError:
        return {"path": str(path), "error": "file unavailable"}
    build_id = None
    try:
        result = subprocess.run(["readelf", "-n", str(path)], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, timeout=10)
        match = re.search(r"Build ID: ([0-9a-fA-F]+)", result.stdout)
        if result.returncode == 0 and match:
            build_id = match.group(1).lower()
    except (OSError, subprocess.TimeoutExpired):
        pass
    return {"path": str(path), "sha256": digest, "build_id": build_id}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save JSON evidence")
    parser.add_argument("--candidate", type=Path,
                        help="Read-only compare a candidate .ko build-id and SHA-256")
    args = parser.parse_args()
    devices = pci_devices()
    loaded_module = module_state()
    candidate = candidate_state(args.candidate)
    if candidate and candidate.get("build_id") and loaded_module.get("build_id"):
        candidate["matches_loaded_module"] = candidate["build_id"] == loaded_module["build_id"]
    checks = {
        "opengl": run(["glxinfo", "-B"]),
        "vulkan": run(["vulkaninfo", "--summary"]),
        "kwin": run(["qdbus6", "org.kde.KWin", "/KWin", "supportInformation"]),
        "gmi": run(["mthreads-gmi"]),
        "module": run(["modinfo", "mtgpu"]),
        "services": run(["systemctl", "is-active", "sddm", "spice-vdagentd", "tailscaled"]),
    }
    render = [x for x in drm_nodes() if x["vendor"] == "0x1ed5" and "/renderD" in x["node"]]
    bound = bool(devices) and all(d["driver"] == "mtgpu" for d in devices)
    software = {}
    for name in ("opengl", "vulkan", "kwin"):
        result = checks[name]
        software[name] = (bool(re.search(r"llvmpipe|softpipe|lavapipe|LLVMpipe", result["output"], re.I))
                          if result["returncode"] == 0 else None)
    # No single node or renderer string proves that the complete desktop/codec path works.
    status = ("NO_MTT_PCI_DEVICE" if not devices else "NO_MTT_KERNEL_DRIVER" if not bound
              else "NO_MTT_RENDER_NODE" if not render else "RENDER_NODE_PRESENT_NEEDS_FUNCTIONAL_TESTS")
    report = {"timestamp_utc": datetime.now(timezone.utc).isoformat(), "kernel": platform.release(),
              "os_release": read(Path("/etc/os-release")), "status": status,
              "mtt_devices": devices, "mtgpu_module": loaded_module,
              "candidate_module": candidate, "drm_nodes": drm_nodes(), "software_renderer_seen": software,
              "checks": checks, "kernel_taint": read(Path("/proc/sys/kernel/tainted"))}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print("状态:", status)
    for d in devices:
        print(f"{d['bdf']}: {d['vendor']}:{d['device']} subsystem={d['subsystem_vendor']}:{d['subsystem_device']} driver={d['driver'] or '未绑定'}")
    if loaded_module.get("present"):
        print(f"mtgpu 模块: state={loaded_module['initstate'] or '未知'} build-id={loaded_module['build_id'] or '未知'}")
    else:
        print("mtgpu 模块: 未加载")
    if candidate:
        print(f"候选模块: sha256={candidate.get('sha256', '不可读')} build-id={candidate.get('build_id') or '未知'} 与已加载模块一致={candidate.get('matches_loaded_module', '无法比较')}")
    print("MTT render 节点:", ", ".join(x["node"] for x in render) or "无")
    print("检测到软件渲染 (null=无法检测):", json.dumps(software, ensure_ascii=False))
    print("完整桌面加速和编解码仍需分别验收。")
    return 2 if not devices or not bound or not render else 0


if __name__ == "__main__":
    sys.exit(main())
