#!/usr/bin/env python3
"""r394 T3: pre-live safety gate + checklist.

The rmmod -f incident: the probe was pinned (ref=1) by a trial whose
firmware-side session had already been cleared, so a normal rmmod failed
with "Device or resource busy". `rmmod -f mt_guest_probe` was used to
force it. The kernel hung -> user cold reboot.

Rules enforced here:
1. No `rmmod -f` / `rmmod --force` in any executable context repo-wide
   (.py/.sh/.c/.h/.mk/Makefile). Force-unload is never the answer; fix
   the refcount (teardown the holder) instead. Prose .md docs may discuss
   the ban (the AGENTS.md red line names it explicitly).
2. scripts/safe_rmmod.sh exists, is executable, and refuses refcount != 0.
3. T1 (test_vm_init_integrity.py) and T2 (test_opcode_whitelist.py) exist:
   the pre-live gate is complete.

PRE-LIVE CHECKLIST (a human reads this before any live round; it is also
summarized in mt-vgpu-guest/reports/r394-live-safety-tests.md):
  [ ] (dm, opcode): every (dm, opcode) this round submits is in PROVEN in
      tests/test_opcode_whitelist.py. If not -> open an offline research
      round first (like r381); NEVER probe an unknown opcode on a live
      trial (r380 lesson).
  [ ] VM creation: does this round create a struct mt_gpu_vm? If yes ->
      confirm it goes through mt_gpu_vm_init() (T1 gate covers in-tree
      code; one-off probe modules must follow the same rule) (r375 lesson).
  [ ] Unload: use scripts/safe_rmmod.sh; NEVER rmmod -f. If refcount != 0
      -> teardown the holder first; if the holder is unknown -> stop the
      round and report (rmmod -f incident lesson).
  [ ] Trial precondition: is 0x890 / firmware state as expected? On
      mismatch -> stop, do not push through (r380's test-2 precondition
      check is what prevented a second accident).
  [ ] One live module at a time; no `timeout` inside ioctl critical
      sections (standing red lines, restated).

Reverse validation: add `rmmod -f foo` to any scanned file other than
the two ALLOWLISTed above -> test_no_rmmod_force_in_repo must FAIL.
"""
import os
import re
import stat
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

FORCE_RE = re.compile(r"\brmmod\s+(?:-[a-z]*f|--force)\b")

# Only executable contexts are scanned: a `rmmod -f` in a script/Makefile
# is actionable; prose discussion of the ban in .md docs is legitimate
# documentation (the AGENTS.md red line names it explicitly on purpose).
# (decompiled/, build/, downloads/ are gitignored bulk; .git is skipped.)
TEXT_SUFFIXES = {".py", ".sh", ".c", ".h", ".mk"}
TEXT_NAMES = {"Makefile"}
SKIP_DIRS = {".git", "decompiled", "build", "downloads"}
# These two files document and enforce the ban, so they may mention the
# pattern in comments/docstrings. Anything else mentioning it fails.
ALLOWLIST = {
    "mt-vgpu-guest/tests/test_pre_live_safety.py",
    "mt-vgpu-guest/scripts/safe_rmmod.sh",
}


def _iter_text_files():
    for p in REPO.rglob("*"):
        if not p.is_file():
            continue
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.suffix in TEXT_SUFFIXES or p.name in TEXT_NAMES:
            yield p


class TestPreLiveSafety(unittest.TestCase):
    def test_no_rmmod_force_in_repo(self):
        hits = []
        for p in _iter_text_files():
            rel = str(p.relative_to(REPO))
            if rel in ALLOWLIST:
                continue
            try:
                src = p.read_text(errors="replace")
            except OSError:
                continue
            for i, line in enumerate(src.splitlines(), 1):
                if FORCE_RE.search(line):
                    hits.append("%s:%d: %s"
                                % (rel, i, line.strip()[:80]))
        self.assertEqual(
            hits, [],
            "rmmod -f/--force is banned repo-wide (r394); "
            "use mt-vgpu-guest/scripts/safe_rmmod.sh:\n" + "\n".join(hits))

    def test_safe_rmmod_helper(self):
        sh = REPO / "mt-vgpu-guest" / "scripts" / "safe_rmmod.sh"
        self.assertTrue(sh.is_file(), "scripts/safe_rmmod.sh missing")
        self.assertTrue(bool(sh.stat().st_mode & stat.S_IXUSR),
                        "scripts/safe_rmmod.sh not executable")
        src = sh.read_text()
        # It must consult lsmod and refuse a non-zero refcount.
        self.assertIn("lsmod", src)
        self.assertRegex(src, r"-ne 0|!=\s*0")
        self.assertIn("refus", src)
        # The only rmmod invocation must be the plain, guarded one.
        # (\brmmod\b so that "safe_rmmod.sh" in the usage line does not
        # match; comments/echoes may mention the banned pattern, see
        # ALLOWLIST.)
        invocations = [l.strip() for l in src.splitlines()
                       if re.search(r"\brmmod\b", l)
                       and not l.strip().startswith("#")
                       and "echo" not in l]
        self.assertEqual(invocations, ['exec sudo -n rmmod "$mod"'],
                         "helper must only rmmod plainly after the refcount check")

    def test_t1_t2_present(self):
        for name in ("test_vm_init_integrity.py", "test_opcode_whitelist.py"):
            self.assertTrue((TESTS / name).is_file(),
                            "%s missing: pre-live gate incomplete" % name)


if __name__ == "__main__":
    unittest.main()
