#!/usr/bin/env python3
"""Check whether the packaged legacy UM uses the 2.3 Guest KMD's PVR connect ABI."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UMD = Path("/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0")
LEGACY_DRI = Path("/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/dri/musa_dri.so")
PACKAGE = Path("/tmp/mtt-linux-umd-5.2.0/mthreads-legacy-umd_5.2.0_amd64.deb")
EXPECTED = {
    "package": "0cb86813a105759cf234a5cde4e26bc33a6f238b7e5d2208b23c5643057fbaeb",
    "srv_um": "b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0",
    "musa_dri": "d1eac49b5ef5e42a5507ff397e7e2dedf7c424eb720073dcad1e4c4e9234fa4c",
}


def sha256(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def disassembly(path, start, stop):
    return subprocess.run(
        ["objdump", "-d", "-M", "intel", f"--start-address={start}",
         f"--stop-address={stop}", str(path)], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def require(condition, message):
    if not condition:
        raise SystemExit("ABI audit failed: " + message)


def main():
    for path in (PACKAGE, UMD, LEGACY_DRI):
        require(path.is_file(), f"missing input {path}")
    hashes = {"package": sha256(PACKAGE), "srv_um": sha256(UMD), "musa_dri": sha256(LEGACY_DRI)}
    require(hashes == EXPECTED, "input hash changed; review the new artifact before trusting offsets")

    init_code = disassembly(UMD, "0x92590", "0x925b0")
    bridge_call = disassembly(UMD, "0x386c0", "0x38780")
    bridge_ioctl = disassembly(UMD, "0x92930", "0x92a10")
    require("0x40046445" in init_code and "0x1" in init_code,
            "legacy UM no longer issues PVR Services init (nr 0x45, init_module 1)")
    require("mov    esi,0x1" in bridge_call and "xor    edx,edx" in bridge_call,
            "Connect bridge ID/function ID changed")
    require("mov    r8d,0x10" in bridge_call and "push   0x11" in bridge_call,
            "Connect input/output sizes changed from 16/17 bytes")
    require("0xc0206440" in bridge_ioctl and "ioctl@plt" in bridge_ioctl,
            "legacy UM no longer issues the PVR SRVKM ioctl (nr 0x40, 32-byte package)")

    # The inputs to the 16-byte BridgeSrvCore Connect call are packed in order:
    # client build options, client DDK build, client DDK version, connection flags.
    connect_code = disassembly(UMD, "0x925c9", "0x925f8")
    require("0x80000850" in connect_code and "0x10000" in connect_code,
            "Connect build options or DDK version changed")

    uapi = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/include/pvr_drm.h"
    bridge = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/generated/common_srvcore_bridge.h"
    version = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/include/pvrversion.h"
    uapi_text, bridge_text, version_text = uapi.read_text(), bridge.read_text(), version.read_text()
    for field in ("__u32 bridge_id", "__u32 bridge_func_id", "__u64 in_data_ptr",
                  "__u64 out_data_ptr", "__u32 in_data_size", "__u32 out_data_size"):
        require(field in uapi_text, f"KMD PVR ioctl field missing: {field}")
    for field in ("ui32ClientBuildOptions", "ui32ClientDDKBuild", "ui32ClientDDKVersion", "ui32Flags"):
        require(field in bridge_text, f"KMD Connect field missing: {field}")
    require("PVRSRV_BRIDGE_SRVCORE_CONNECT\t\t\t\tPVRSRV_BRIDGE_SRVCORE_CMD_FIRST+0" in bridge_text,
            "KMD Connect function ID is not zero")
    require("#define PVRVERSION_MAJ               1U" in version_text and
            "#define PVRVERSION_MIN               0U" in version_text,
            "KMD PVR DDK version is not 1.0")

    report = {
        "legacy_umd_package_sha256": hashes["package"],
        "libsrv_um_sha256": hashes["srv_um"],
        "musa_dri_sha256": hashes["musa_dri"],
        "umd_pvr_init_ioctl": "0x40046445",
        "umd_pvr_srvkm_ioctl": "0xc0206440",
        "connect_bridge_id": 1,
        "connect_function_id": 0,
        "connect_input_size": 16,
        "connect_output_size": 17,
        "client_ddk_version": "0x10000",
        "kmd_ddk_version": "1.0 (0x10000)",
        "connect_abi_matches": True,
        "scope": "Static PVR Connect/init ABI match only; does not prove all bridge calls, Guest operation, or rendering compatibility.",
    }
    out = ROOT / "reports/legacy-umd-pvr-connect-abi.json"
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
