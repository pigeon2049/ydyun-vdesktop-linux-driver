#!/usr/bin/env python3
"""Gate the argument contract PVRSRVConnectionCreateDevice actually has.

The bA13 recipe called it as `PVRSRVConnectionCreateDevice(b7, u0, u0)` and that
looked right for many rounds -- because every offline run used the fabricating
shim, which answered every open regardless of the index it was given. Against a
real driver the index matters:

    UMD 0xa4af0:   lea    -0x80(%rdi),%eax
                   cmp    $0x3f,%eax
                   ja     ->1            ; reject anything outside 0x80..0xbf
                   ...                   ; else snprintf("/dev/dri/renderD%d")

so the second argument is a DRM *node index*, and 0 is out of range. The UMD
then reports MTSRV_ERROR_INIT_FAILURE (4) from inside ConnectionCreate, before
it issues a single ioctl -- which is why this looked like a busid, an
enumeration, or a udev problem for two rounds.

This test pins the *shape* of the contract, not a magic number: the render
minor must be inside the 0x80..0xbf window the UMD accepts, and it must be the
minor of the node that is actually present. It fails if someone reintroduces
`u0` in a live command, and it fails if the node moves to a minor the UMD would
reject.

The `u0` form still appears in reports/ as a *historical record* of what the bA13
runs actually did. Those lines are kept, because rewriting history would hide
why the recipe was wrong, and they are annotated in place instead. Only a `u0`
occurrence inside a fenced shell block counts as a live command.
"""
import os
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# The UMD's window, from the disassembly quoted above.
ACCEPT_LO = 0x80
ACCEPT_HI = 0xBF


def render_minor_from_devices():
    """Minor number of the first /dev/dri/renderD* node, or None."""
    dri = Path("/dev/dri")
    if not dri.is_dir():
        return None
    for node in sorted(dri.glob("renderD*")):
        m = re.fullmatch(r"renderD(\d+)", node.name)
        if m:
            return int(m.group(1))
    return None


class DeviceConnArgContract(unittest.TestCase):
    def test_render_minor_is_inside_the_window_the_umd_accepts(self):
        minor = render_minor_from_devices()
        if minor is None:
            self.skipTest("no /dev/dri/renderD* node present (module not loaded)")
        self.assertGreaterEqual(
            minor, ACCEPT_LO,
            f"renderD{minor} is below the UMD's accepted window; the UMD "
            f"computes index-0x80 and rejects anything under {ACCEPT_LO:#x}, so "
            "the device connection would fail with INIT_FAILURE")
        self.assertLessEqual(
            minor, ACCEPT_HI,
            f"renderD{minor} is above the UMD's accepted window "
            f"({ACCEPT_HI:#x})")

    def test_no_live_command_passes_zero_as_the_node_index(self):
        """The bA13 recipe used u0 here; keep that mistake from coming back.

        Prose and historical notes are allowed to mention it -- they are the
        record of what went wrong. A command someone could paste is not.
        """
        offenders = []
        for path in sorted(list(ROOT.glob("**/*.md")) + list(ROOT.glob("**/*.sh"))):
            try:
                lines = path.read_text(errors="ignore").splitlines()
            except OSError:
                continue
            in_block = False
            for i, line in enumerate(lines, 1):
                if line.lstrip().startswith("```"):
                    in_block = not in_block
                    continue
                if not in_block:
                    continue
                if re.search(r"PVRSRVConnectionCreateDevice\s*\(?\s*b7\s*,?\s*u0",
                             line):
                    offenders.append(f"{path.relative_to(ROOT)}:{i}")
        self.assertEqual(
            offenders, [],
            "PVRSRVConnectionCreateDevice is called with u0 as the node index "
            f"in a runnable command: {offenders}. Pass the render minor "
            "(e.g. u130); the UMD requires it within 0x80..0xbf.")

    def test_the_window_matches_the_disassembly(self):
        """Keep the constants honest if this file is ever edited."""
        self.assertEqual((ACCEPT_LO, ACCEPT_HI), (0x80, 0xBF))
        self.assertEqual(ACCEPT_HI - ACCEPT_LO, 63,
                         "the UMD's cmp is against 0x3f, i.e. 64 minors wide")


if __name__ == "__main__":
    unittest.main()
