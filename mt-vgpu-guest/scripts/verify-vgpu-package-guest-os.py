#!/usr/bin/env python3
"""Audit the local MT vGPU 2.3.0 ZIP for Guest/Host driver payloads.

This is an archive-content check only. It does not install or execute payloads.
"""
from __future__ import annotations

import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "downloads/S2000_MT_vGPU_2.3.0.zip"
PACKAGE_ROOT = "S2000 MT vGPU 2.3.0/MT vGPU 2.3.0/"
GUEST_PREFIX = PACKAGE_ROOT + "MT_vGPU_GUEST_v2.3.0/"
DKMS_PREFIX = PACKAGE_ROOT + "MT_vGPU_DKMS_v2.3.0/"
EXPECTED_WINDOWS_GUEST_DRIVERS = {
    "mtkm64.sys",
    "mtdispkm64.sys",
    "mtvpukm64.sys",
}
LINUX_PACKAGE_SUFFIXES = (
    ".ko", ".deb", ".rpm", ".run", ".sh", ".tar.gz", ".tar.xz", ".tar.zst",
)


def audit_member_names(member_names: list[str]) -> dict:
    guest_files = sorted(
        name for name in member_names
        if name.startswith(GUEST_PREFIX) and not name.endswith("/")
    )
    if not guest_files:
        raise ValueError("MT_vGPU_GUEST_v2.3.0 payload is absent from the archive")

    guest_basenames = {Path(name).name for name in guest_files}
    missing = sorted(EXPECTED_WINDOWS_GUEST_DRIVERS - guest_basenames)
    if missing:
        raise ValueError(f"expected Windows Guest driver files are missing: {missing}")

    linux_guest_payloads = sorted(
        name for name in guest_files
        if name.lower().endswith(LINUX_PACKAGE_SUFFIXES)
    )
    if linux_guest_payloads:
        raise ValueError(
            f"Linux driver-like payload found under Guest directory: {linux_guest_payloads}"
        )

    linux_host_packages = sorted(
        name for name in member_names
        if name.startswith(DKMS_PREFIX)
        and name.lower().endswith((".deb", ".rpm"))
    )
    if not linux_host_packages:
        raise ValueError("Linux DKMS Host package is absent from the expected directory")

    return {
        "archive": str(ARCHIVE),
        "archive_guest_directory": GUEST_PREFIX,
        "windows_guest_driver_files": sorted(EXPECTED_WINDOWS_GUEST_DRIVERS),
        "linux_guest_driver_like_payloads": linux_guest_payloads,
        "linux_guest_driver_package_present_in_this_archive": False,
        "linux_host_dkms_packages": linux_host_packages,
        "scope": "this local S2000 MT vGPU 2.3.0 ZIP only; no claim about other releases",
        "executed_or_installed_payloads": False,
    }


def main() -> None:
    try:
        with zipfile.ZipFile(ARCHIVE) as archive:
            result = audit_member_names(archive.namelist())
    except (OSError, zipfile.BadZipFile, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
