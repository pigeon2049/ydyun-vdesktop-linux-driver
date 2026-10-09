"""r416: T2 readback verification tests."""
import re
import unittest
from pathlib import Path

from tests.helpers import get_kernel_dir, get_repo_root

KERNEL = get_kernel_dir()
REPO = get_repo_root()


def _read(name):
    return (KERNEL / name).read_text()


def _define_value(fname, name):
    text = _read(fname)
    m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+|\d+)" % name, text)
    assert m, "%s not defined in %s" % (name, fname)
    return int(m.group(1), 0)


class TestTaReadbackGate(unittest.TestCase):
    def test_readback_debug_default_off(self):
        # r416: MT_TA_READBACK_DEBUG must default to 0 (no debug ioctl).
        self.assertEqual(
            _define_value("mt_ta_real.h", "MT_TA_READBACK_DEBUG"), 0)

    def test_t2_target_constants(self):
        # r416: 64x64 RGBA8 render target.
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_WIDTH"), 64)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_HEIGHT"), 64)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_BYTES"),
                         64 * 64 * 4)
        self.assertEqual(_define_value("mt_ta_real.h", "MT_T2_TARGET_BO_SLOT"), 11)

    def test_q0_flag_bits_inferred(self):
        # r416: Q0 flag bits from r410 ([INFERRED], TO-VALIDATE).
        text = _read("mt_ta_real.h")
        self.assertIn("MT_TA_ENTRY_Q0_FLAG_BITS", text)
        self.assertIn("[INFERRED]", text)
        self.assertEqual(
            _define_value("mt_ta_real.h", "MT_TA_ENTRY_Q0_FLAG_BITS"),
            0x48000000000)


class TestTaReadbackRequest(unittest.TestCase):
    def test_request_has_target_va(self):
        # r416: target_va added to mt_ta_real_request (0 = none).
        text = _read("mt_ta_real.h")
        self.assertIn("u64 target_va;", text)

    def test_q0_setter_exists(self):
        text = _read("mt_ta_real.h")
        self.assertIn("mt_ta_entry_simple_set_target", text)

    def test_buffer_build_takes_target(self):
        # r416: buffer builder accepts target_va.
        text = _read("mt_ta_real.h")
        m = re.search(
            r"mt_ta_real_buffer_build\(unsigned char \*buf, u32 w, u32 h,\s*\n?\s*u32 n_entries, u64 target_va\)",
            text)
        self.assertIsNotNone(m, "buffer_build must take target_va")


class TestTaReadbackKernel(unittest.TestCase):
    def test_bo_read_helper_exists(self):
        # r416: pvr_translator_bo_read mirrors bo_write (pre-existing).
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        self.assertIn("pvr_translator_bo_read", text)

    def test_render_context_target_fields(self):
        # r416: 12th BO fields on render context.
        text = _read("mt_render_context.h")
        self.assertIn("struct mt_bo target_bo;", text)
        self.assertIn("u64 target_va;", text)
        self.assertIn("bool target_ready;", text)

    def test_debug_ioctl_gated(self):
        # r416: 0xFD handler inside #if MT_TA_READBACK_DEBUG.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("pvr_cmd_ta_readback(struct mt_pvr_file")
        self.assertGreater(idx, 0)
        before = text[max(0, idx - 600):idx]
        self.assertIn("#if MT_TA_READBACK_DEBUG", before)

    def test_dispatch_case_gated(self):
        # r416: 0xFD dispatch case gated (uses MT_PVR_FN_DEBUGTAREADBACK macro).
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("case MT_PVR_FN_DEBUGTAREADBACK:")
        self.assertGreater(idx, 0)
        before = text[max(0, idx - 200):idx]
        self.assertIn("#if MT_TA_READBACK_DEBUG", before)

    def test_submit_real_passes_target(self):
        # r416: mt_ta_submit_real forwards req->target_va.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        self.assertIn("req->target_va", text)


class TestTaReadbackUserspace(unittest.TestCase):
    def test_tool_exists(self):
        p = REPO / "userspace" / "mt-ta-readback.c"
        self.assertTrue(p.exists(), "mt-ta-readback.c missing")

    def test_makefile_target(self):
        text = (REPO / "userspace" / "Makefile").read_text()
        self.assertIn("mt-ta-readback", text)

    def test_tool_writes_ppm(self):
        # r416: tool writes P6 PPM and verifies pixels.
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn('P6\\n', text)
        self.assertIn("nonzero_pixels", text)

    def test_tool_handles_enotty(self):
        # r416: tool degrades gracefully when debug ioctl absent.
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn("MT_TA_READBACK_DEBUG=1", text)
        self.assertIn("ENOTTY", text)




class TestTargetBoLifecycle(unittest.TestCase):
    """r417: 12th BO (T2 render target) lifecycle hardening."""

    def _bridge(self):
        return (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()

    def _fn_body(self, text, sig, last=False):
        # r417: create/destroy have forward decls; anchor on the
        # definition (last occurrence, or a sig ending with "\n{").
        idx = text.rfind(sig) if last else text.find(sig)
        self.assertGreater(idx, 0, sig)
        end = text.find("\n}\n", idx)
        self.assertGreater(end, idx)
        return text[idx:end]

    def _destroy_body(self, text):
        sig = ("static void mt_render_context_destroy("
               "struct mt_pvr_render_context *ctx)\n{")
        return self._fn_body(text, sig)

    def test_target_bo_bound_after_11_before_exec(self):
        # r417: the 12th BO must bind after the 11-BO loop and before the
        # exec process creation — the VM rejects binds once active_uses>0.
        body = self._fn_body(
            self._bridge(),
            "static int mt_render_context_create(struct mt_pvr_file *file,",
            last=True)
        bo_loop = body.find("for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)")
        tgt = body.find("r416: T2 render target (12th BO). Bound here")
        execproc = body.find(
            "mt_execution_process_create(&d->execution, &ctx->process,")
        self.assertGreater(bo_loop, 0)
        self.assertGreater(tgt, bo_loop,
                           "target BO must bind after the 11-BO loop")
        self.assertGreater(execproc, tgt,
                           "target BO must bind before exec process creation")

    def test_target_bo_slot_no_collision(self):
        # r417: slot 11 is the first VA slot after the 11-BO array
        # (indices 0..10); if BO_COUNT ever grows, this invariant breaks.
        text = (KERNEL / "mt_gfx_context.h").read_text()
        m = re.search(r"#define\s+MT_GFX_CONTEXT_BO_COUNT\s+(\d+)U", text)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 11)
        self.assertEqual(
            _define_value("mt_ta_real.h", "MT_T2_TARGET_BO_SLOT"), 11)

    def test_target_bo_destroy_reverse_order(self):
        # r417: destroy releases in reverse creation order: 11 BOs, then
        # the target BO, then the VM (which drops binding refs).
        body = self._destroy_body(self._bridge())
        rel_loop = body.find("for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)")
        tgt_rel = body.find("if (ctx->target_ready)")
        vm_fini = body.find("mt_render_context_vm_destroy(ctx->vm);")
        self.assertGreater(rel_loop, 0)
        self.assertGreater(tgt_rel, rel_loop,
                           "target BO released after the 11 BOs")
        self.assertGreater(vm_fini, tgt_rel,
                           "VM torn down after the target BO")

    def test_target_bo_bind_failure_no_leak(self):
        # r417: if the target bind fails, the BO is put before rollback
        # and target_ready stays false so destroy won't double-put.
        body = self._fn_body(
            self._bridge(),
            "static int mt_render_context_create(struct mt_pvr_file *file,",
            last=True)
        idx = body.find("r416: target BO bind failed")
        self.assertGreater(idx, 0)
        window = body[idx:idx + 250]
        self.assertIn("mt_bo_put(&ctx->target_bo);", window)
        ready = body.find("ctx->target_ready = true;")
        self.assertGreater(ready, idx,
                           "target_ready set only after bind success")

    def test_rollback_reuses_destroy(self):
        # r417: create's out_rollback reuses the guarded destroy path,
        # so a half-initialized context (incl. 12th BO) is safe.
        text = self._bridge()
        idx = text.find("out_rollback:")
        self.assertGreater(idx, 0)
        self.assertIn("mt_render_context_destroy(ctx);",
                      text[idx:idx + 200])

    def test_target_va_uses_slot_stride(self):
        # r417: target VA = vm_base_va + slot * stride (slot 11).
        text = self._bridge()
        self.assertIn(
            "(u64)MT_T2_TARGET_BO_SLOT * MT_RENDER_CONTEXT_VA_STRIDE",
            text)


class TestDebugIoctlValidation(unittest.TestCase):
    """r417: 0x82:0xFD parameter validation hardening."""

    def _handler(self):
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("static int pvr_cmd_ta_readback(struct mt_pvr_file")
        self.assertGreater(idx, 0)
        end = text.find("\n}\n", idx)
        self.assertGreater(end, idx)
        return text[idx:end]

    def test_0xfd_rejects_bad_context(self):
        body = self._handler()
        idx = body.find("if (!robj || !robj->render_ctx)")
        self.assertGreater(idx, 0)
        self.assertIn("-EINVAL", body[idx:idx + 120])

    def test_0xfd_requires_target_ready(self):
        body = self._handler()
        idx = body.find("if (!rctx->target_ready)")
        self.assertGreater(idx, 0)
        self.assertIn("-ENODEV", body[idx:idx + 120])

    def test_0xfd_params_flow_to_validation(self):
        # r417: width/height/n_entries are forwarded (not silently
        # clamped); mt_ta_real_buffer_build validates them (behavior
        # proven by the C tests in pvr_bridge_core_test).
        body = self._handler()
        self.assertIn("req.width = in.width;", body)
        self.assertIn("req.height = in.height;", body)
        self.assertIn("req.n_entries = in.n_entries;", body)
        self.assertIn("mt_ta_submit_real(file, &req, &fence)", body)

    def test_0xfd_gate_consistency(self):
        # r417: the dispatch case guard must match the handler's guard.
        # Latent build break caught here: the case was under
        # MT_TA_READBACK_DEBUG alone while the handler needs
        # MT_TA_READBACK_DEBUG && MT_TA_REAL_PACKET.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()

        def guard_before(pos):
            line_start = text.rfind("#if", 0, pos)
            line_end = text.find("\n", line_start)
            return text[line_start:line_end]

        fn_pos = text.find("static int pvr_cmd_ta_readback(struct mt_pvr_file")
        case_pos = text.find("case MT_PVR_FN_DEBUGTAREADBACK:")
        self.assertGreater(fn_pos, 0)
        self.assertGreater(case_pos, 0)
        fn_guard = guard_before(fn_pos)
        case_guard = guard_before(case_pos)
        self.assertEqual(case_guard, fn_guard,
                         "dispatch case guard must match handler guard")
        self.assertIn("MT_TA_READBACK_DEBUG", case_guard)
        self.assertIn("MT_TA_REAL_PACKET", case_guard)

    def test_0xfd_has_submit_real_forward_decl(self):
        # r417: pvr_cmd_ta_readback calls mt_ta_submit_real, which is
        # defined later in the file; the double-gated region needs a
        # forward declaration or the (1,1) build breaks.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        fn_pos = text.find("static int pvr_cmd_ta_readback(struct mt_pvr_file")
        self.assertGreater(fn_pos, 0)
        decl = "static int mt_ta_submit_real(struct mt_pvr_file *file,"
        decl_pos = text.find(decl)
        self.assertGreater(decl_pos, 0)
        self.assertLess(decl_pos, fn_pos,
                        "forward decl must precede the 0xFD handler")
        guard = text[text.rfind("#if", 0, decl_pos):decl_pos]
        self.assertIn("MT_TA_READBACK_DEBUG", guard)
        self.assertIn("MT_TA_REAL_PACKET", guard)

    def test_0xfd_absent_returns_enotty(self):
        # r417: with the gate off, 0xFD is not dispatched -> -ENOTTY,
        # which the userspace tool degrades on (exit 2).
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("static int pvr_dispatch_rgxta3d(")
        end = text.find("\n}\n", idx)
        body = text[idx:end]
        didx = body.find("default:")
        self.assertGreater(didx, 0)
        self.assertIn("return -ENOTTY;", body[didx:didx + 60])


class TestTaSubmitRealTargetVa(unittest.TestCase):
    """r417: mt_ta_submit_real target_va integration (source level;
    behavior proven by the C tests in pvr_bridge_core_test)."""

    def _fn(self):
        # r417: anchor on the __maybe_unused definition, not the
        # forward declaration added for the 0xFD handler.
        text = (KERNEL / "recovery" / "mt_pvr_bridge.c").read_text()
        idx = text.find("__maybe_unused static int mt_ta_submit_real(")
        self.assertGreater(idx, 0)
        end = text.find("\n}\n", idx)
        self.assertGreater(end, idx)
        return text[idx:end]

    def test_submit_real_null_guards(self):
        body = self._fn()
        idx = body.find("if (!file || !req || !out_fence)")
        self.assertGreater(idx, 0)
        self.assertIn("-EINVAL", body[idx:idx + 80])

    def test_submit_real_validates_n_entries(self):
        body = self._fn()
        self.assertIn("req->n_entries == 0", body)
        self.assertIn("req->n_entries > MT_TA_REAL_MAX_ENTRIES", body)

    def test_submit_real_forwards_target_va(self):
        body = self._fn()
        self.assertIn(
            "mt_ta_real_buffer_build(ta_buf, req->width, req->height,",
            body)
        self.assertIn("req->target_va)", body)

    def test_submit_real_checks_staging_bo(self):
        # r417: BO[10] must be ready (r412 staging decision: VM sealed,
        # reuse the mapped BO).
        body = self._fn()
        self.assertIn("rctx->bos_ready[MT_TA_REAL_STAGING_BO_INDEX]", body)


class TestReadbackAnalyzeUnit(unittest.TestCase):
    """r417: pixel analysis extracted for unit testing."""

    def test_analyze_header_exists(self):
        p = REPO / "userspace" / "ta_readback_analyze.h"
        self.assertTrue(p.exists(), "ta_readback_analyze.h missing")

    def test_tool_uses_shared_analyze(self):
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn("ta_readback_analyze.h", text)
        self.assertIn("ta_readback_analyze(rbout->pixels", text)

    def test_tool_enotty_mentions_both_gates(self):
        # r417: the 0xFD handler needs both gates; the hint must say so.
        text = (REPO / "userspace" / "mt-ta-readback.c").read_text()
        self.assertIn("MT_TA_READBACK_DEBUG=1", text)
        self.assertIn("MT_TA_REAL_PACKET=1", text)


if __name__ == "__main__":
    unittest.main()
