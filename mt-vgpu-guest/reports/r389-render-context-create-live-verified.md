# r389: Render Context Create Realized — 11 BOs Live Verified (No Oops)

## Conclusion

`0x82:0x12` (RGXCreateRenderContext2) now allocates real per-context state
(R6 Route A): 11 BOs (86,300B) with initial data, bound to a per-context VM,
CSW built, exec process/context created (node_type=5 → DM2). Live V1 single
create succeeded with zero oops/WARN.

## Implementation

**`kernel/recovery/mt_pvr_bridge.c`** (+~200 lines):
- `mt_render_context_vm_create(d, pt_bo)`: per-context VM with
  `d->buffers`-backed page tables (64KB). Unlike r376's synthetic VM
  (`pvr_gpu_plan_bo_ops`), the tables BO uses `d->buffers.ops` so that
  `mt_gpu_vm_bind_many()` accepts the 11 real BOs (store+ops must match).
- `mt_render_context_vm_destroy()`: reverse teardown.
- `mt_render_context_create(file, ctx)`: the 8-step R6-2 flow —
  1. VM create (64KB PT)
  2-4. Loop 11×: `mt_bo_create` → `pvr_translator_bo_write` (init data)
      → `mt_gpu_vm_bind_many` at `0x70000000 + i*16MB`
  5. `mt_gfx_context_build_csw()`
  6-7. `mt_execution_process_create` + `mt_execution_context_create(5, 0)`
  8. `resources_ready = true` (only on full success)
  - Any failure → reverse-order rollback, no half-initialized state.
- `pvr_cmd_render2_create()`: now calls `mt_render_context_create()` instead
  of minting an empty token. On failure, cleans up obj + render_ctx.

**`kernel/mt_render_context.h`** (+3 lines):
- Added `struct mt_bo pt_bo` field (page-table BO must outlive the VM).

## Live V1 Validation

Single `0x82:0x12` via Python harness (INIT module=2, 12B IN):
```
[4161.921444] r389: render ctx VM created, base_va=0x70000000
[4161.921607] r389: BO 0 bound va=0x70000000 bytes=4096
[4161.921895] r389: BO 1 bound va=0x71000000 bytes=8192
[4161.921983] r389: BO 2 bound va=0x72000000 bytes=4096
... (BO 3-9) ...
[4161.925615] r389: BO 10 bound va=0x7a000000 bytes=8192
[4161.925616] r389: CSW built
[4161.925618] r389: exec process/context created (node_type=5)
[4161.925619] r389: render context READY (11 BOs, CSW, exec)
```
- Harness: `handle=0x1000 error=0` (success)
- dmesg: zero WARN/BUG/Oops
- Bridge reloaded once (r360 flow); probe untouched (trial pinned)

## Design Corrections During Implementation

1. **Store/ops mismatch**: r376's synthetic VM (`pvr_gpu_plan_bo_ops`) cannot
   bind real `d->buffers` BOs — `mt_gpu_vm_bind_many` requires
   `bo->store == vm->tables->store && bo->ops == vm->tables->ops`.
   Fixed by allocating page-table BO from `d->buffers`.
2. **VM capacity**: 16KB page tables only held 2 ranges (ENOSPC at BO 2).
   Increased to 64KB — holds 11+ ranges comfortably.
3. **Exec process store check**: `mt_execution_process_create` requires
   `vm->tables->store == s->buffers` (`d->execution.buffers`). Satisfied by
   the `d->buffers`-backed tables BO.

## Gates

- `make -C mt-vgpu-guest check-offline`: **450 Python + 299 C, all green**
  (+9 new r389 tests, +1 layout update for pt_bo field)
- `make kernel` W=1: **zero warnings**
- Reverse validation: breaking the `mt_render_context_create` call → test
  failure; restored → green.

## Honest Boundaries

- V1 only: single create validated. Destroy (R6-3), kick-side parsing (R6-4),
  multi-context isolation (V3), and file-close cleanup (V4) are future rounds.
- Probe ref 1→13 after V1 (11 BOs + VM + exec hold refs); R6-3 destroy must
  release them. Not a leak — expected until destroy is implemented.
- `0x82:0x14` (3D) still uses observer; kick-side context parsing not yet wired.
- Per-context VA base is `0x70000000` (V1 single context); multi-context will
  use `0x70000000 + N*16MB` per r387 §3.3.

## Evidence

- `reports/r389-dmesg-v1.txt` (0600): 23 lines, full V1 dmesg trace
