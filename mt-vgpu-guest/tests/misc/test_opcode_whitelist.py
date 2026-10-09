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


def _function_span(src, name):
    """(start, end) offsets of the body of `name(` ... matching close brace."""
    i = src.index(name + "(")
    i = src.index("{", i)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return (i, j + 1)
    raise AssertionError("unbalanced braces in %s" % name)


def _struct_init(src, name):
    """Text of `name = { ... };` with balanced braces (r403 ops tables)."""
    i = src.index(name + " = {")
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    raise AssertionError("unbalanced braces in %s" % name)


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
        # r403: dm pinning lives in the TA ops table, not a wrapper local.
        fence = _strip_comments(_read("mt_marker_fence.h"))
        ops = _struct_init(fence, "mt_ta_submit_ops")
        self.assertIn(".dm = MT_FW_DM_TA", ops)
        # mt_fw_ta_marker_command() is called only from the TA build hook
        # (definition excluded); the hook is referenced only from the ops
        # table, so the opcode cannot reach any other submit path.
        bstart, bend = _function_span(fence, "mt_ta_submit_build")
        for m in re.finditer(r"mt_fw_ta_marker_command\(", fence):
            before = fence[max(0, m.start() - 80):m.start()]
            if re.search(r"\bvoid\s*$", before):
                continue  # the definition itself: `static inline void <name>(`
            self.assertTrue(bstart <= m.start() < bend,
                            "mt_fw_ta_marker_command called outside TA build hook")
        refs = re.findall(r"\bmt_ta_submit_build\b", fence)
        self.assertEqual(len(refs), 2,
                         "mt_ta_submit_build referenced outside mt_ta_submit_ops")

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
        # r403: dm pinning lives in the 3D ops table.
        ops = _struct_init(fence, "mt_3d_submit_ops")
        self.assertIn(".dm = MT_FW_DM_3D", ops)
        # r380 mirror: the DM2 ops table must never carry the TA opcode,
        # neither directly nor through the hooks it names.
        self.assertNotIn("MT_FW_TA_OPCODE", ops)
        self.assertNotIn("mt_fw_ta_marker_command", ops)
        self.assertNotIn("mt_ta_submit_build", ops)
        for hm in re.finditer(r"\.\w+\s*=\s*(\w+)\s*,", ops):
            hname = hm.group(1)
            if hname.startswith("mt_3d_submit_"):
                hbody = _function_body(fence, hname)
                self.assertNotIn("MT_FW_TA_OPCODE", hbody,
                                 "TA opcode reachable via 3D hook %s" % hname)
                self.assertNotIn("mt_fw_ta_marker_command", hbody,
                                 "TA builder reachable via 3D hook %s" % hname)
        # The thin 3D wrapper itself must stay TA-free as well.
        body = _function_body(fence, "mt_marker_submit_3d_work")
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
        # For each FORBIDDEN (dm, op): no ops table pinned to that dm may
        # reference the opcode's symbol, neither directly nor through the
        # hooks it names. (r403: dm pinning moved from wrapper locals into
        # per-engine ops tables. Literal uses are covered because
        # test_ta_opcode_pinned_to_dm3 confines the symbol's users.)
        dm_defines = {0: None, 1: None, 2: "MT_FW_DM_3D", 3: "MT_FW_DM_TA"}
        fence = _strip_comments(_read("mt_marker_fence.h"))
        for (dm, op), why in FORBIDDEN.items():
            sym = OPCODE_SYMBOLS[op]
            marker = dm_defines[dm]
            for m in re.finditer(
                    r"static const struct mt_marker_submit_ops (\w+)\s*=\s*\{",
                    fence):
                ops = _struct_init(fence, m.group(1))
                if marker and (".dm = %s" % marker) in ops:
                    self.assertNotIn(
                        sym, ops,
                        "forbidden pair (%d, %#x) has a code path via %s (%s)"
                        % (dm, op, m.group(1), why))
                    self.assertNotIn("mt_fw_ta_marker_command", ops)
                    for hm in re.finditer(r"\.\w+\s*=\s*(\w+)\s*,", ops):
                        hname = hm.group(1)
                        if re.search(r"\b(static|inline)\b[^{};]*\b%s\s*\(" % hname,
                                     fence):
                            hbody = _function_body(fence, hname)
                            self.assertNotIn(
                                sym, hbody,
                                "forbidden pair (%d, %#x) via hook %s (%s)"
                                % (dm, op, hname, why))
                            self.assertNotIn("mt_fw_ta_marker_command", hbody)

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
