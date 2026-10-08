# r391: Kick-Side render_ctx Parsing Live Verified (V3 Isolation, No Oops)

## Conclusion

`pvr_cmd_musakickgfx2` (0x82:0xC) now resolves `h_render_context` via
`pvr_object_find(..., MT_PVR_KIND_CONTEXT)`. When the object carries a
`resources_ready` render_ctx, the kick uses the per-context VM; otherwise
it falls back to the per-file VM (Phase 1, marker protected). Live
validation: marker regression (no-ctx + ctx kicks) all pass, V3
two-context isolation confirmed (separate VMs, no state leak), dmesg
clean.

## Implementation

`kernel/recovery/mt_pvr_bridge.c` (+45/-17, in `pvr_cmd_musakickgfx2`):

1. **Resolve**: `pvr_object_find(file, in.h_render_context, MT_PVR_KIND_CONTEXT)` → `obj->render_ctx`.
2. **Select VM**: if `rctx && rctx->resources_ready && rctx->vm`, `kick_vm = rctx->vm`
   (log "r391: kick with render_ctx vm_base_va=..."); else `kick_vm = file->ta_vm_ctx`
   (per-file fallback, r376 V1 preserved).
3. **V2 bind validation** runs on the selected VM (`kick_vm->vm`).

**Design note (honest)**: the TA marker op requires `work->context->route.dm == MT_FW_DM_TA`,
but `render_ctx->exec_ctx` is node_type 5 (DM 3D). Phase 1 uses the per-context VM for
mapping isolation; the marker keeps its TA-dm context. Real exec_ctx submission arrives
with 3D kick enablement (R6-5).

`0x82:0x14`: unchanged (r215 observer); design recorded only.

## Live Validation (bridge reloaded once, r391 build)

**Marker regression** (Python harness, `build/traces/r391/harness.py`):
- create1: handle=0x1000, create2: handle=0x1001 (both 11-BO contexts)
- kick[no-ctx] (h_render_context=0): error=0, fence=1 → per-file fallback
  (dmesg: "R5 V1: TA VM context created")
- kick[ctx1] (0x1000): error=0, fence=2 → per-context VM
  (dmesg: "r391: kick with render_ctx vm_base_va=0x70000000")
- kick[ctx2] (0x1001): error=0, fence=3 → per-context VM
- OUT.update_fence matches dmesg wire (1/2/3).

**V3 isolation**: two contexts, separate `mt_bridge_ta_vm` instances
(dmesg: two "render ctx VM created"). Both use `vm_base_va=0x70000000` by design —
isolation is at the VM/page-table level (Route A), not the VA level. Kicks resolve to
the correct VM per handle. No cross-context state leak observed. File-close destroyed
both contexts (probe ref 25→13, r390 V2b path).

- dmesg: zero WARN/BUG/Oops. Bridge ref 0, probe ref 13 (baseline).

## Gates

- `make -C mt-vgpu-guest check-offline`: 463 Python + 299 C, all green
  (+5 new `test_kick_render_ctx.py`).
- `make kernel` W=1: zero warnings.
- Reverse validation: breaking `pvr_object_find` → test failure; restored → green.

## Honest Boundaries

- `exec_ctx` not yet used for TA submission (dm mismatch, by design).
- VA ranges overlap across contexts (same `vm_base_va`); isolation is via separate
  page tables. Firmware-side VA translation still TO-VALIDATE (no query interface).
- `0x82:0x14` still observer.

## Evidence

- `reports/r391-dmesg-kick-v3.txt` (0600): dmesg excerpts.
- Harness: `build/traces/r391/harness.py` (gitignored, not committed).
