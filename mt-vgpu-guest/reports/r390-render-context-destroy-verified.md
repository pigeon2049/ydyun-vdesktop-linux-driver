# r390: Render Context Destroy Realized — V2 Live Verified (No Leaks)

## Conclusion

`mt_render_context_destroy()` (R6-3) tears down per-context state in strict
reverse order of creation. Live V2 validated both destroy paths with zero
leaks: explicit `0x82:0x13` destroy and file-close cleanup both returned
probe refs 25→13 (delta -12, all 11 BOs + VM + exec released). dmesg clean.

## Implementation

**`kernel/recovery/mt_pvr_bridge.c`** (+65/-16 lines):

1. **`mt_render_context_destroy(ctx)`** (new): reverse-order teardown —
   - exec context → exec process (process destroy decrements `vm->owners`,
     must precede VM fini which returns `-EBUSY` while `owners>0`);
   - 11 BOs via `mt_bo_put` (VM holds one ref per binding; `mt_gpu_vm_fini`
     drops those);
   - per-context VM via `mt_render_context_vm_destroy` (fini + pt_bo put).
   - Safe on partial init (`resources_ready=false`): `exec_ready` /
     `bos_ready[]` / `vm`-NULL guards skip whatever was never built.
   - All state cleared; second call is a no-op.
   - `WARN_ON` on exec/vm destroy failures (refcount bug detector).

2. **`mt_render_context_vm_destroy`**: now `WARN_ON` if `mt_gpu_vm_fini`
   fails (was silently ignored).

3. **Create rollback refactored**: `out_rollback` now delegates to
   `mt_render_context_destroy(ctx)` — single teardown path, no duplication.
   (r389's `test_rollback` updated to match.)

4. **`pvr_cmd_handle_release` hook**: when `kind == MT_PVR_KIND_CONTEXT`
   and `obj->render_ctx != NULL`, calls destroy + frees the struct.
   Legacy token objects (`render_ctx == NULL`) skip this. Covers both
   `0x82:0x13` (legacy) and `0x82:0x1x` DDK2 destroy paths.

5. **`pvr_file_release` hook (V4)**: file-close cleanup destroys
   `render_ctx` for any leaked objects. Without this, `rmmod` leaks
   the 11 BOs + VM + exec (observed: probe ref stuck at 13 after r389 V1).

## Live V2 Validation

Bridge reloaded once (r360 flow, r390 build). Python harness:

**V2a — explicit destroy (`0x82:0x13`):**
```
probe ref before create: 13
create: handle=0x1000 error=0
probe ref after create: 25 (delta=+12)
destroy: error=0
probe ref after destroy: 13 (delta=-12)
V2a PASS
```

**V2b — file-close without explicit destroy:**
```
create2: handle=0x1001 error=0
probe ref after create2: 25
fd closed without explicit destroy
probe ref after close: 13 (delta=-12)
V2b PASS
```

- dmesg: zero WARN/BUG/Oops since boot.
- Bridge ref 0, probe ref 13 (baseline; see Honest Boundaries).

## Gates

- `make -C mt-vgpu-guest check-offline`: **458 Python + 299 C, all green**
  (+8 new r390 tests, +1 r389 test updated for refactored rollback).
- `make kernel` W=1: **zero warnings**.
- Reverse validation: breaking the destroy hook → test failure; restored → green.

## Honest Boundaries

- Probe ref baseline is 13, not 1: r389 V1's render context leaked (no
  destroy existed then) and survives in the probe's store. The V2 delta
  (25→13) conclusively proves the new destroy path releases everything
  it allocated. The old 12-ref leak clears on next cold reboot.
- V2 did not test destroy of a *partially-initialized* context (would
  require fault injection); the guards are code-reviewed, not live-fired.
- Multi-context isolation (V3) and kick-side parsing (R6-4) remain future.
- `0x82:0x14` (3D) still uses observer.

## Evidence

- `reports/r390-dmesg-v2.txt` (0600): full dmesg, 869 lines, zero WARN/BUG/Oops.
