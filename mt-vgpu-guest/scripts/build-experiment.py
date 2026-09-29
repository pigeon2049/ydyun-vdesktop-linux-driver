#!/usr/bin/env python3
"""Build-only experiment for the pinned community source; never installs/loads it."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "099f7ea5afced34f9424618a9e2ec7aa37dba024"


def edit(path, old, new, patches, base):
    before = path.read_text()
    if before.count(old) != 1:
        raise RuntimeError(f"Unexpected source: {path}: expected exactly one {old!r}")
    after = before.replace(old, new)
    path.write_text(after)
    name = str(path.relative_to(base))
    patches.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                      fromfile="a/" + name, tofile="b/" + name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["native", "guest-macro", "guest-header"])
    parser.add_argument("--source", type=Path, default=ROOT / "src/mtgpu-2.7.1-6.12")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be 1..16")
    source = args.source.resolve()
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True)
    if commit != COMMIT or dirty:
        parser.error("Source must be a clean checkout of pinned commit " + COMMIT)
    kernel = platform.release()
    if platform.machine() != "x86_64" or not Path(f"/lib/modules/{kernel}/build/Makefile").exists():
        parser.error("Requires x86_64 and headers for the running kernel")
    # This API adaptation was checked against this exact Debian backport.
    if kernel != "6.12.107+deb13-amd64":
        parser.error("Recheck kernel APIs before adapting a different kernel")
    target = ROOT / "build" / (args.mode + "-" + time.strftime("%Y%m%d-%H%M%S"))
    shutil.copytree(source, target, ignore=shutil.ignore_patterns(".git"))
    patches = []
    edit(target / "src/common/os-interface.c", "pci_resize_resource(dev,  resno,  size)",
         "pci_resize_resource(dev, resno, size, 0)", patches, target)
    # Hard guard: even an accidental insmod cannot register/probe any PCI device.
    edit(target / "src/mtgpu/mtgpu_drv.c", "static int __init mtgpu_driver_init(void)\n{",
         "static int __init mtgpu_driver_init(void)\n{\n"
         "\tpr_err(\"mtgpu: offline build experiment; hardware initialization forbidden\\n\");\n"
         "\treturn -EPERM;", patches, target)
    if args.mode != "native":
        edit(target / "inc/config_kernel.h", "#define RGX_NUM_OS_SUPPORTED 1\n",
             "#define RGX_NUM_OS_SUPPORTED 8\n", patches, target)
        edit(target / "inc/config_kernel.h", "#define PVRSRV_APPHINT_DRIVERMODE 0x7FFFFFFF\n",
             "#define PVRSRV_APPHINT_DRIVERMODE 1\n", patches, target)
        # 8 only exercises the >1 branches. It is not an inferred Host OS count.
    header_sha256 = None
    if args.mode == "guest-header":
        # Offline-only follow-up: restore the missing debug declarations from the
        # official 2.3.0 Host source. This does NOT repair the binary core ABI.
        header = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/services/rgxdebug.h"
        header_sha256 = hashlib.sha256(header.read_bytes()).hexdigest()
        shutil.copyfile(header, target / "inc/pvr/services/rgxdebug.h")
    reports = ROOT / "reports"
    reports.mkdir(exist_ok=True)
    (reports / (args.mode + ".patch")).write_text("".join(patches))
    scratch = target / "scratch"
    scratch.mkdir()
    env = dict(os.environ, PWD=str(target), TMPDIR=str(scratch))
    log_path = reports / (args.mode + "-build.log")
    command = ["make", f"-j{args.jobs}", "ARCH=x86_64", "KERNELVER=" + kernel]
    with log_path.open("w") as log:
        result = subprocess.run(command, cwd=target, env=env, stdout=log, stderr=subprocess.STDOUT)
    output = log_path.read_text(errors="replace")
    module = target / "mtgpu.ko"
    summary = {
        "mode": args.mode, "kernel": kernel, "source_commit": commit,
        "directory": str(target.relative_to(ROOT)), "returncode": result.returncode,
        "module_created": module.exists(), "hardware_init_disabled_in_source": True,
        "installed": False, "loaded": False,
        "borrowed_header_sha256": header_sha256,
        "objtool_warning_count": len(re.findall(r"warning: objtool:", output)),
        "errors": [line for line in output.splitlines() if re.search(r"error:|ERROR:|undefined!", line)][:60],
    }
    if module.exists():
        summary["module_sha256"] = hashlib.sha256(module.read_bytes()).hexdigest()
        modinfo = shutil.which("modinfo") or "/usr/sbin/modinfo"
        summary["vermagic"] = subprocess.check_output([modinfo, "-F", "vermagic", str(module)], text=True).strip()
    (reports / (args.mode + "-build.json")).write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
