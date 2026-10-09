#!/usr/bin/env python3
"""r394 T2 gate: firmware (dm, opcode) whitelist.

r380 postmortem: a one-off probe submitted opcode 0x66 (the TA opcode) on
DM2 (the 3D queue). Firmware ignored the packet AND cleared the trial
session (0x890 2->0) -> user cold reboot.

Rule enforced here: every (dm, opcode) pair submitted to firmware by
in-tree code must be in PROVEN below. A new pair may be added only after
an offline research round (e.g. r381 determined 0x68 for 3D) or live
validation (e.g. r365 proved DM3/0x66). The r380 pair (2, 0x66) is
explicitly FORBIDDEN and must never be added.

Note on scope: the round brief listed {(3,0x66),(3,0x64),(2,0x68)}; TQX
(1,0x67) and trial (0,0x46/0x47) are included here because they are
live-proven (r37-r41, every boot). Anything else needs provenance first.

Reverse validation: change MT_FW_DM_TA to 2U -> test_ta_opcode_pinned_to_dm3
must FAIL.
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_kernel_dir

KERNEL = get_kernel_dir()

# (dm, opcode) -> provenance. Admission rule: see module docstring.
PROVEN = {
    (0, 0x46): "trial connect (MT_FW_CONNECT), live every boot",
    (0, 0x47): "trial disconnect (MT_FW_DISCONNECT), live",
    (1, 0x67): "TQX transfer (work type 1), r37-r41 live",
    (1, 100): "TQX null-descriptor marker via mt_live_marker (default dm=1)",
    (2, 0x68): "3D compute (work type 5, MT_FW_3D_OPCODE), r381 code-proven",
    (3, 0x64): "TA standard COMPLETE, r365 live",
    (3, 0x66): "TA marker (MT_FW_TA_OPCODE), r365 live",
}
# The r380 accident. Firmware ignores it and clears the trial session.
FORBIDDEN = {
    (2, 0x66): "r380",
}
# Opcode value -> the C symbol in-tree code must use for it (so that a new
# literal 0x66 appearing anywhere is itself suspicious).
OPCODE_SYMBOLS = {
    0x66: "MT_FW_TA_OPCODE",
    0x68: "MT_FW_3D_OPCODE",
}


def _read(rel):
    return (KERNEL / rel).read_text()


def _define_value(header, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+|\d+)U?",
                  _read(header))
    assert m, "define %s not found in %s" % (name, header)
    return int(m.group(1), 0)


def _function_body(src, name):
    """Extract the body of `name(` ... matching close brace."""
    i = src.index(name + "(")
    i = src.index("{", i)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    raise AssertionError("unbalanced braces in %s" % name)


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"(?m)//.*$", "", src)
    return src


class TestOpcodeWhitelist(unittest.TestCase):
    def test_ta_opcode_pinned_to_dm3(self):
        # r380 regression: the TA opcode must only ever go out on DM3.
        self.assertEqual(_define_value("mt_ta_submit.h", "MT_FW_TA_OPCODE"), 0x66)
        dm = _define_value("mt_ta_submit.h", "MT_FW_DM_TA")
        self.assertEqual(dm, 3)
        self.assertIn((dm, 0x66), PROVEN)
        # MT_FW_TA_OPCODE must not be referenced outside its own header and
        # the TA submit path. Any new user is an unproven submission.
        users = sorted(p.name for p in
                       list(KERNEL.rglob("*.c")) + list(KERNEL.rglob("*.h"))
                       if p.name != "mt_ta_submit.h"
                       and "MT_FW_TA_OPCODE" in p.read_text())
        self.assertEqual(users, ["mt_marker_fence.h"],
                         "MT_FW_TA_OPCODE used outside TA path: %s" % users)
        # The TA submit op pins dm to MT_FW_DM_TA (a const, not a variable).
        fence = _strip_comments(_read("mt_marker_fence.h"))
        body = _function_body(fence, "mt_marker_submit_ta_work")
        self.assertIn("const u32 dm = MT_FW_DM_TA;", body)
        # mt_fw_ta_marker_command() is called only from the TA submit path
        # (definition excluded).
        ta_start = fence.index("static int mt_marker_submit_ta_work")
        ta_end = fence.index("static int mt_marker_submit_3d_work")
        for m in re.finditer(r"mt_fw_ta_marker_command\(", fence):
            before = fence[max(0, m.start() - 80):m.start()]
            if re.search(r"\bvoid\s*$", before):
                continue  # the definition itself: `static inline void <name>(`
            self.assertTrue(ta_start < m.start() < ta_end,
                            "mt_fw_ta_marker_command called outside TA submit path")

    def test_3d_opcode_pinned_to_dm2(self):
        self.assertEqual(_define_value("mt_3d_submit.h", "MT_FW_3D_OPCODE"), 0x68)
        dm = _define_value("mt_3d_submit.h", "MT_FW_DM_3D")
        self.assertEqual(dm, 2)
        self.assertIn((dm, 0x68), PROVEN)
        users = sorted(p.name for p in
                       list(KERNEL.rglob("*.c")) + list(KERNEL.rglob("*.h"))
                       if p.name != "mt_3d_submit.h"
                       and "MT_FW_3D_OPCODE" in p.read_text())
        self.assertEqual(users, ["mt_marker_fence.h"],
                         "MT_FW_3D_OPCODE used outside 3D path: %s" % users)
        fence = _strip_comments(_read("mt_marker_fence.h"))
        body = _function_body(fence, "mt_marker_submit_3d_work")
        self.assertIn("const u32 dm = MT_FW_DM_3D;", body)
        # r380 mirror: the DM2 submit path must never carry the TA opcode.
        self.assertNotIn("MT_FW_TA_OPCODE", body)
        self.assertNotIn("mt_fw_ta_marker_command", body)

    def test_trial_opcodes_on_dm0(self):
        self.assertEqual(_define_value("mt_fw_queue.h", "MT_FW_CONNECT"), 0x46)
        self.assertEqual(_define_value("mt_fw_queue.h", "MT_FW_DISCONNECT"), 0x47)
        self.assertIn((0, 0x46), PROVEN)
        self.assertIn((0, 0x47), PROVEN)
        trial = _read("mt_fw_trial.h")
        self.assertRegex(trial, r"mt_fw_queue_try_submit\(t->queue,\s*0,",
                         "mt_trial_send must submit on dm 0")

    def test_forbidden_pairs_have_no_symbolic_path(self):
        # For each FORBIDDEN (dm, op): every function pinned to that dm must
        # not reference the opcode's symbol. (Literal uses are covered
        # because test_ta_opcode_pinned_to_dm3 confines the symbol's users.)
        dm_defines = {0: None, 1: None, 2: "MT_FW_DM_3D", 3: "MT_FW_DM_TA"}
        fence = _strip_comments(_read("mt_marker_fence.h"))
        for (dm, op), why in FORBIDDEN.items():
            sym = OPCODE_SYMBOLS[op]
            marker = dm_defines[dm]
            for m in re.finditer(r"static int (\w+)\(", fence):
                body = _function_body(fence, m.group(1))
                if marker and ("const u32 dm = %s;" % marker) in body:
                    self.assertNotIn(
                        sym, body,
                        "forbidden pair (%d, %#x) has a code path via %s (%s)"
                        % (dm, op, m.group(1), why))

    def test_dual_exec_ctx_dm_pinning(self):
        """r397: render_ctx carries two exec contexts pinned to their DMs.

        exec_ctx_ta  (node_type=2) must route to DM3 (TA).
        exec_ctx_3d  (node_type=5) must route to DM2 (3D).
        The route table (mt_work_command.h) is the source of truth;
        the create calls in mt_pvr_bridge.c must use the right node types.
        Reverse validation: change node_type 2->5 in the TA create call
        must FAIL this test.
        """
        bridge = _read("recovery/mt_pvr_bridge.c")
        # TA context created with node_type=2
        self.assertRegex(
            bridge,
            r"mt_execution_context_create\(&ctx->exec_ctx_ta,\s*&ctx->process,\s*2,\s*0\)",
            "exec_ctx_ta must be created with node_type=2",
        )
        # 3D context created with node_type=5
        self.assertRegex(
            bridge,
            r"mt_execution_context_create\(&ctx->exec_ctx_3d,\s*&ctx->process,\s*5,\s*0\)",
            "exec_ctx_3d must be created with node_type=5",
        )
        # Route table: type 2 -> dm 3, type 5 -> dm 2
        route = _read("mt_work_command.h")
        self.assertRegex(route, r"case 2: r\.dm = 3;",
                         "node_type 2 must route to DM3")
        self.assertRegex(route, r"case 5: r\.dm = 2;",
                         "node_type 5 must route to DM2")
        # The TA kick gate requires dm==3; the real ctx satisfies it.
        # (2,0x66) remains forbidden (r380); TA opcode only on DM3.
        self.assertIn((3, 0x66), PROVEN)
        self.assertIn((2, 0x66), FORBIDDEN)


if __name__ == "__main__":
    unittest.main()
