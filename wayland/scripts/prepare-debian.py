#!/usr/bin/env python3
"""Prepare the pinned upstream Debian tree for the YDYUN build (idempotent)."""
from pathlib import Path
import re
import shutil
import subprocess
import sys

BASE = "0.22.1-4.1"
VERSION = BASE + "+ydyun1"
source = Path(sys.argv[1]).resolve()
debian = source / "debian"
current = subprocess.check_output(
    ["dpkg-parsechangelog", "-S", "Version"], cwd=source, text=True
).strip()
if current not in (BASE, VERSION):
    sys.exit(f"Unsupported source version {current}; expected {BASE} or {VERSION}")
if current == BASE:
    changelog = debian / "changelog"
    changelog.write_text(
        f"spice-vdagent ({VERSION}) trixie; urgency=medium\n\n"
        "  * Add the KDE Plasma Wayland KScreen monitor configuration patch.\n"
        "  * Use a distinct downstream version and depend on kscreen.\n"
        "  * Ship an APT preference that excludes unpatched builds while\n"
        "    permitting future +ydyun releases.\n\n"
        " -- YDYUN Linux Adapter <root@localhost>  Tue, 22 Sep 2026 22:00:00 +0800\n\n"
        + changelog.read_text()
    )
control = debian / "control"
content = control.read_text()
if not re.search(r"^Depends:.*\bkscreen\b", content, re.M):
    content, count = re.subn(r"^Depends: ", "Depends: kscreen, ", content, count=1, flags=re.M)
    if count != 1:
        sys.exit("Could not add kscreen dependency")
control.write_text(content)
shutil.copyfile(
    Path(__file__).resolve().parents[1] / "packaging/ydyun-spice-vdagent.pref",
    debian / "ydyun-spice-vdagent.pref",
)
manifest = debian / "spice-vdagent.install"
entry = "debian/ydyun-spice-vdagent.pref etc/apt/preferences.d"
lines = manifest.read_text().splitlines()
if entry not in lines:
    manifest.write_text("\n".join(lines + [entry]) + "\n")
print(f"Prepared spice-vdagent {VERSION}")
