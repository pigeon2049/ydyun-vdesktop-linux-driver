# r439: TA Header +0x50/+0x58 tile packing implemented (offline)

> Round r439 (2026-10-09). **纯离线。** Implements r438 P0: UMD tile packing
> (`FUN_00184220` [MEASURED]) into TA Header+0x50/+0x58.

## Conclusion

**`mt_ta_real_buffer_build()` now writes tile-packed w/h to Header+0x50/+0x58.**

r438 proved (`linux-legacy-umd-5.2.0/decompiled.c:58215`) that UMD packs
`((psKickTA[3 or 4] + 0x3f) >> 6) & 0x3f` into bits 48-53 of the Header
qwords at +0x50/+0x58. Our Header-only wrote 0 there = "0 tiles" — the
strongest new suspect for the firmware hang behind the r425/r432/r436
timeouts. Formula and bit48-53 [MEASURED]; tile-count semantics
((x+63)/64) [INFERRED]; approximating psKickTA[3]/[4] with the request
w/h [INFERRED].

## Changes

### `kernel/mt_ta_real.h`
- New: `MT_TA_BUF_HDR_TILE_PACK_X 0x50U`, `MT_TA_BUF_HDR_TILE_PACK_Y 0x58U`
  (offsets [MEASURED] r438).
- New: `static inline u64 mt_ta_tile_pack(u32 x)` =
  `(((u64)(((x + 0x3fU) >> 6) & 0x3fU)) << 48`.
- `mt_ta_real_buffer_build()`: after the +0x10 write, writes
  `mt_ta_tile_pack(w)` to +0x50 and `mt_ta_tile_pack(h)` to +0x58.
  64x64 -> `0x0001000000000000` at both.
- Doc comment: +0x50/+0x58 documented ([MEASURED] formula / [INFERRED]
  semantics); stale "pre-filled 0xFFFFFFFF" note (r431 misread — code fixed
  in r434 but this comment lagged) corrected to per-dword `0x00000001`
  [MEASURED] r433/r434; w/h now feed tile packing (no longer "unused").

### `tests/ta/test_header_integrity.py` (T5 gate)
- `test_header_write_whitelist`: approved set extended from {+0x10} to
  {`MT_TA_BUF_HDR_TARGET_VA`, `MT_TA_BUF_HDR_TILE_PACK_X`,
  `MT_TA_BUF_HDR_TILE_PACK_Y`} — named constants only.
- New `test_tile_pack_constants`: offsets must be 0x50/0x58.
- New `test_header_tile_pack_writes`: +0x50/+0x58 must be written from
  `mt_ta_tile_pack(w)` / `mt_ta_tile_pack(h)`.
- New `test_tile_pack_helper_defined`: helper exists and carries the UMD
  formula (`0x3fU`, `<< 48`).

### `tests/c/pvr_bridge_core_test.c`
- New `test_ta_tile_pack` (10 checks): 64->1<<48, 128->2<<48, 1->1<<48,
  0->0, 65->2<<48, 3840->60<<48, 4096->0 (6-bit wrap: 64&0x3f=0),
  4097->1<<48 (65&0x3f=1), 0x8000->0 (512&0x3f=0) + bitmask check.
- `test_ta_real_buffer_build_target` rewritten for tile writes: +0x10==0 /
  +0x50/+0x58==tile_pack assertions replace the old all-zero loop;
  idempotency loop now skips all three written ranges via new
  `ta_hdr_written_byte()` helper (the old duplicated 360B loop was
  collapsed — dynamic check count 1491->783, coverage preserved via
  targeted assertions).
- Registered `test_ta_tile_pack` in main.

## Gates
- `make -C mt-vgpu-guest check-offline`: **557 Python + 783 C checks green**
  (Python +3; C dynamic count changed 1491->783 because the rewritten
  buffer_build test replaced two 360-iteration byte loops with targeted
  assertions — no coverage lost).
- `make -C mt-vgpu-guest kernel W=1`: **zero warnings**.
- Reverse validation: tile writes forced to `0` -> `test_header_tile_pack_writes`
  FAILs precisely ("T5 FAIL: Header+0x50 not written from
  mt_ta_tile_pack(w) (r439)"); restored -> green.

## Honest boundaries
- Tile-count semantics ((x+63)/64) [INFERRED]; w/h stand in for
  psKickTA[3]/[4] [INFERRED] (r438).
- +0x68 (boolean, DDK-set value [UNKNOWN]) and +0x120 (flags bit-pack) stay 0
  (r438 P1/P2, not this round).
- Zero hardware touched, pure offline. No production behavior change beyond
  the two new Header qwords (gated paths only).
- Live validation deferred (r440+): r436 left bridge ref=1; needs user cold
  reboot before any live round.
