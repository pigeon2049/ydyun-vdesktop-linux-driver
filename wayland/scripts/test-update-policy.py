#!/usr/bin/env python3
"""Exercise real APT selection against an isolated, synthetic package index."""
from pathlib import Path
import getpass
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
(root / "build").mkdir(exist_ok=True)
version = "0.22.1-4.1+ydyun1"


def record(name, ver, installed=False):
    fields = [f"Package: {name}", f"Version: {ver}", "Architecture: amd64",
              "Maintainer: Policy test <test@example.invalid>", "Description: synthetic APT test"]
    if installed:
        fields.append("Status: install ok installed")
    else:
        fields += [f"Filename: {name}_{ver}_amd64.deb", "Size: 1", "SHA256: " + "0" * 64]
    return "\n".join(fields) + "\n\n"


with tempfile.TemporaryDirectory(prefix="apt-policy-", dir=root / "build") as tmp:
    tree = Path(tmp)
    tree.chmod(0o755)
    for sub in ("etc/apt/sources.list.d", "etc/apt/preferences.d", "var/lib/apt/lists/partial",
                "var/lib/dpkg", "var/cache/apt/archives/partial", "var/log/apt", "repo"):
        (tree / sub).mkdir(parents=True, exist_ok=True)
    policy = tree / "etc/apt/preferences.d/ydyun-spice-vdagent.pref"
    shutil.copyfile(root / "wayland/packaging/ydyun-spice-vdagent.pref", policy)
    (tree / "etc/apt/sources.list").write_text(f"deb [trusted=yes] file:{tree}/repo ./\n")
    options = ["-o", f"Dir={tree}", "-o", f"Dir::State::status={tree}/var/lib/dpkg/status",
               "-o", "Debug::NoLocking=1", "-o", "APT::Architecture=amd64",
               "-o", f"APT::Sandbox::User={getpass.getuser()}", "-o", "APT::Get::List-Cleanup=0"]

    def apt(tool, *args):
        return subprocess.check_output([tool, *options, *args], text=True, stderr=subprocess.STDOUT)

    def scenario(installed, available, expected, protected=True):
        (tree / "var/lib/dpkg/status").write_text(
            record("spice-vdagent", installed, True) + record("unrelated-test", "1", True))
        (tree / "repo/Packages").write_text(
            "".join(record("spice-vdagent", v) for v in available) + record("unrelated-test", "2"))
        apt("apt-get", "update")
        out = apt("apt-cache", "policy", "spice-vdagent")
        assert f"Candidate: {expected}\n" in out, out
        upgrade = apt("apt-get", "-s", "upgrade")
        assert "Inst unrelated-test [1] (2" in upgrade, upgrade
        if protected:
            assert "Inst spice-vdagent" not in upgrade, upgrade
        print(f"PASS installed={installed}, available={available}, candidate={expected}")

    # Legacy package migrates normally to the first downstream build.
    scenario("0.22.1-4.1", ["0.22.1-4.1", version], version, False)
    assert f"({version} " in apt("apt-get", "-s", "install", "spice-vdagent")
    # Including a much newer upstream version cannot replace the patch.
    scenario(version, ["0.22.1-4.1", "99.0-1"], version)
    # Later downstream releases remain installable without unhold or repinning.
    next_version = "0.22.1-4.1+ydyun2"
    scenario(version, ["99.0-1", next_version], next_version, False)
    assert f"({next_version} " in apt("apt-get", "-s", "install", "spice-vdagent")
    # Removing the configuration restores normal upstream selection.
    policy.rename(tree / "saved-policy.pref")
    scenario(version, ["99.0-1"], "99.0-1", False)
    # Explicit upstream downgrade is possible after disabling the policy.
    scenario(version, ["0.22.1-4.1"], version)
    assert "(0.22.1-4.1 " in apt("apt-get", "-s", "--allow-downgrades", "install",
                                "spice-vdagent=0.22.1-4.1")
    print("PASS explicit upstream rollback; all tests left host APT untouched")
