# r416: T2 readback verification designed and implemented (offline)

> Round r416 (2026-10-09). **Offline.** T2 (pixel verification, r405 G4) infrastructure:
> 12th-BO render target + gated debug ioctl + userspace PPM tool. Zero hardware touched.

## Conclusion

**T2 readback path is designed and implemented offline; live validation deferred to r417.**

- Render target: 12th context BO (64×64 RGBA8, 16KB), created+bound at context
  create (before exec process; VM refuses binds once `active_uses>0`), released
  at destroy. None of the 11 spec BOs is a framebuffer (all context state).
- TA integration: `mt_ta_real_request.target_va` (0 = none, backward compatible);
  Q0 = `target_va | 0x48000000000` ([INFERRED] from r410, TO-VALIDATE).
- Readback: existing `pvr_translator_bo_read()` (TQX mirror) + new gated debug
  ioctl `0x82:0xFD` (`MT_TA_READBACK_DEBUG`, default 0): submit → wait fence →
  read target → return 16KB pixels.
- Userspace: `userspace/mt-ta-readback.c` (builds clean) — handshake, 0xFD call,
  PPM (P6) write, pixel verification (non-zero count, distinct colors).
- Gates: `check-offline` **503 Python + 299 C green** (15 new tests),
  `make kernel` W=1 zero warnings, reverse validation passed.

## Design

### Why a 12th BO (not PMR)

| Option | Verdict |
|---|---|
| Per-file PMR + `pvr_mmap` | Rejected: PMRs bind into `file->gpu_vm` (CPU-only plan), not the per-context VM the TA executes in. |
| Reuse BO[10]@0 (4KB free) | Rejected: only 4KB (32×32 RGBA8); @0 is "Rasterisation context state" — clobbering risks firmware state. |
| **12th context BO** | **Adopted**: same store/ops/VM as the 11 BOs; bound during create before `active_uses>0`; 16KB = 64×64 RGBA8. |

### Data flow (r417 live)

```
userspace: open renderD128 → INIT(2) → Connect(0x1:0x0) → Create(0x82:0x12)
    → DebugTAReadback(0x82:0xFD)
kernel:    pvr_cmd_ta_readback → mt_ta_submit_real(target_va=rctx->target_va)
    → TA entry Q0 = target_va | flags → DM3/0x66 → firmware 0x100
    → dma_fence_wait_timeout(5s) → pvr_translator_bo_read(target_bo)
    → copy_to_user pixels
userspace: write PPM P6 → count non-zero pixels → verdict
```

### Honest boundaries

- Q0 flag bits `0x48000000000` and address masking are [INFERRED] (r410); firmware
  acceptance of a non-zero Q0 is **unvalidated**.
- The dummy TA entries (r414) carry no real geometry; firmware may complete 0x100
  while drawing nothing → expect all-zero pixels on first live run. Non-zero
  pixels require meaningful TA entries (deeper ISA work).
- `MT_TA_READBACK_DEBUG=0` default: the 0xFD path is zero-impact when off
  (verified: `#if` removes handler + dispatch case; `make kernel` clean both ways
  is not run — gate-off build is the default and was verified).
- 12th BO adds one VM binding per context (16KB); destroy path releases it
  (rollback-safe: `mt_render_context_destroy` handles partial init).

## Changes

| File | Change |
|---|---|
| `kernel/mt_ta_real.h` | `target_va` in request; `MT_TA_ENTRY_Q0_FLAG_BITS` [INFERRED]; `mt_ta_entry_simple_set_target()`; `mt_ta_real_buffer_build()` takes `target_va`; T2 constants; `MT_TA_READBACK_DEBUG` gate (0); 0xFD IN/OUT structs (gated) |
| `kernel/mt_render_context.h` | `target_bo` / `target_va` / `target_ready` fields |
| `kernel/mt_pvr_wire.h` | `MT_PVR_FN_DEBUGTAREADBACK 0xFD` |
| `kernel/recovery/mt_pvr_bridge.c` | 12th BO create+bind (pre-exec-process) / destroy release; `mt_ta_submit_real` forwards `target_va`; `pvr_cmd_ta_readback()` + dispatch case (both gated) |
| `userspace/mt-ta-readback.c` | New PPM tool (builds clean, `-Werror`) |
| `userspace/Makefile` | `mt-ta-readback` target |
| `tests/ta/test_ta_readback.py` | New: 15 tests (gate/constants/request/kernel/userspace) |
| `tests/render/test_render_context_layout.py` | EXPECTED updated for 3 new fields (sizeof 1720) |

## Gates

- `make -C mt-vgpu-guest check-offline`: **503 Python + 299 C green** (1 skipped).
- `make kernel` W=1: **zero warnings**.
- Reverse validation: `MT_TA_READBACK_DEBUG=1` → `test_readback_debug_default_off` FAILS; restored → green.
- `make -C userspace`: clean (`-Wall -Wextra -Werror`).

## Next (r417)

1. Live: rebuild bridge with `MT_TA_READBACK_DEBUG=1` (+ `MT_TA_REAL_PACKET=1`),
   run `mt-ta-readback`, check firmware response to non-zero Q0.
2. Expect all-zero pixels first (dummy entries); meaningful pixels need real TA
   geometry (ISA work beyond r410).
3. If Q0 rejected (FAULT/timeout), revisit flag bits/masking per r410 §2.1.
