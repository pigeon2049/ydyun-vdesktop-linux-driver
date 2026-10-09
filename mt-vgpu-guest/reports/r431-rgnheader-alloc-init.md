# r431: RgnHeader allocation + init implemented, TA Header +0x10 points to RgnHeader (offline)

**Conclusion**: The 13th BO (RgnHeader) is implemented in the Linux guest render
context: 0x100B for 64x64, pre-filled 0xFFFFFFFF ([MEASURED] r430,
InitRegionHeaderBuffer), bound at VA slot 12 (0x7c000000). TA Header +0x10 now
receives the RgnHeader VA (no longer the raw 16KB pixel BO). r425's timeout
(feeding pixels as region headers) cannot recur through this path.

## 1. Changes

### kernel/mt_ta_real.h
- New constants [MEASURED] (r430):
  - `MT_TA_RGNHEADER_TILE_BYTES 0x40`, `MT_TA_RGNHEADER_ALIGN 64`
  - `MT_TA_RGNHEADER_WIDTH/HEIGHT 64`, `MT_TA_RGNHEADER_BYTES 0x100`
  - `MT_TA_RGNHEADER_BO_SLOT 12` (VA 0x7c000000)
  - `MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFF`
- New `mt_ta_rgnheader_size(w,h)` = `round_up(tilesX*tilesY*0x40, 64)`,
  tilesX=(w+0x1f)>>5 (RGXRenderTargetInitConfig [MEASURED]).
- `mt_ta_real_buffer_build()` doc: target_va MUST be RgnHeader VA.
- `struct mt_ta_real_request.target_va` doc: RgnHeader VA (r416 pixel-BO
  semantic retired).

### kernel/mt_render_context.h
- `struct mt_pvr_render_context` += `rgnheader_bo`, `rgnheader_va`,
  `rgnheader_ready` (13th BO). sizeof 1720 -> 1824 (layout test updated).

### kernel/recovery/mt_pvr_bridge.c
- `mt_render_context_create()`: allocate 13th BO (PAGE_ALIGN(0x100)),
  memset 0xFF init (InitRegionHeaderBuffer behavior), bind at slot 12
  before exec process creation (VM still accepts binds).
- `mt_render_context_destroy()`: release RgnHeader BO (mirrors target BO).
- `pvr_cmd_ta_readback()` (0x82:0xFD): require `rgnheader_ready`;
  `req.target_va = rctx->rgnheader_va` (was `rctx->target_va` pixel BO).
  The 12th BO remains for post-completion pixel readback.

## 2. Tests
- C `test_ta_rgnheader_size`: 64x64->0x100, 128x128->0x400,
  65x65->0x240, 32x32->0x40, 1x1->0x40, 96x64->0x180.
- C `test_ta_rgnheader_init_pattern`: 0xFF fill == all 0xFFFFFFFF dwords.
- Python layout test: new offsets pinned (rgnheader_bo@1624,
  rgnheader_va@1712, rgnheader_ready@1720, pt_bo@1728,
  resources_ready@1816, sizeof 1824).

## 3. Gates
- `make -C mt-vgpu-guest check-offline`: **550 Python + 1490 C green**
  (C 1416 -> 1490, +74 checks from new tests; 1 skipped).
- `make -C mt-vgpu-guest kernel W=1`: **zero warnings**.
- T5 whitelist: unaffected (buffer_build structure unchanged).

## 4. Honest boundaries
- `+0x28`/`+0x30` still [UNKNOWN] (kept zero); r430 P1 pending.
- RgnHeader per-dword semantics [UNKNOWN] (firmware-private); all-ones
  is UMD behavior [MEASURED], not firmware requirement proof.
- Live validation deferred to r432 (dual-gate rebuild + 0xFD live).
- **Zero hardware touched, pure offline.**
