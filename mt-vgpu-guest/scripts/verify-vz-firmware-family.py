#!/usr/bin/env python3
"""Audit the packaged VZ firmware families against the S3000 PCI profile."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0"
FIRMWARE = ROOT / "src/official-vgpu-2.3.0/usr/lib/firmware/mthreads"


def define(text, name):
    match = re.search(rf"^#define\s+{re.escape(name)}\s+\((0x[0-9a-fA-F]+)\)",
                      text, re.MULTILINE)
    if not match:
        raise AssertionError(f"missing definition: {name}")
    return int(match.group(1), 16)


def main():
    defs = (SOURCE / "inc/mtgpu/mtgpu_defs.h").read_text()
    pci = (SOURCE / "src/mtgpu/mtgpu_drv.c").read_text()
    module = (SOURCE / "src/mtgpu/mtgpu_drv.c").read_text()
    params = (SOURCE / "src/mtgpu/mtgpu_module_param.c").read_text()

    s3000 = define(defs, "DEVICE_ID_MTT_S3000")
    quyuan1 = define(defs, "DEVICE_ID_QUYUAN1")
    pinghu1 = define(defs, "DEVICE_ID_PINGHU1")
    assert s3000 == 0x0222 and quyuan1 == 0x0200 and pinghu1 == 0x0400
    s3000_entries = re.findall(
        r"\{\s*PCI_VENDOR_ID_MT,\s*DEVICE_ID_MTT_S3000,\s*"
        r"PCI_ANY_ID,\s*PCI_ANY_ID,\s*"
        r"PCI_CLASS_DISPLAY_(?:3D|VGA)\s*<<\s*8,\s*~0,\s*"
        r"\.driver_data\s*=\s*\(unsigned long\)&quyuan1_drvdata\s*\}",
        pci,
    )
    assert len(s3000_entries) == 2, (
        "expected S3000 entries in both the 3D and VGA PCI tables to use "
        "quyuan1_drvdata"
    )

    declared = re.findall(r'MODULE_FIRMWARE\(FIRMWARE_LOAD_PATH\("([^"]+)"\)\)',
                          module)
    vz_names = sorted(name for name in declared if ".vz." in name)
    assert vz_names == ["musa.fw.1.0.0.0.vz.linux", "musa.fw.1.0.0.0.vz.win"]
    assert re.search(r"bool\s+mtgpu_load_windows_firmware\s*=\s*true", params)

    variants = [
        ("musa.fw.1.0.0.0.vz.win", "rgx_firmware_vgpu"),
        ("musa.fw.1.1.0.0.vz.win", "rgx_firmware_qy2_vgpu"),
        ("musa.fw.1.2.0.0.vz.win", "rgx_firmware_ph_vgpu"),
    ]
    blobs = []
    for filename, marker in variants:
        path = FIRMWARE / filename
        blob = path.read_bytes()
        assert marker.encode() in blob, f"{filename}: missing build marker {marker}"
        blobs.append({
            "filename": filename,
            "size": len(blob),
            "sha256": hashlib.sha256(blob).hexdigest(),
            "embedded_build_marker": marker,
        })

    report = {
        "source_package": "official-vgpu-2.3.0",
        "target_pci_id": f"1ed5:{s3000:04x}",
        "source_platform_family": "QUYUAN1",
        "source_platform_family_id": f"1ed5:{quyuan1:04x}",
        "source_device_data": "quyuan1_drvdata",
        "other_family_id": f"1ed5:{pinghu1:04x}",
        "driver_declared_vz_firmware": vz_names,
        "windows_firmware_default": True,
        "packaged_vz_windows_variants": blobs,
        "selection_note": (
            "Keep the driver's declared 1.0.0.0 VZ image for the QUYUAN1/S3000 path. "
            "The 1.1 and 1.2 images identify QY2 and PH firmware builds; the local source "
            "does not establish them as compatible with this S3000 virtual device."
        ),
        "limits": [
            "Build markers identify source firmware families, not a full compatibility ABI.",
            "No firmware was uploaded or selected for a hardware trial by this script.",
            "The Windows WDDM loader images in /opt/MTT-driver-only are separate binaries "
            "from these Linux package VZ firmware blobs."
        ],
    }
    out = ROOT / "reports/vz-firmware-family-validation.json"
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
