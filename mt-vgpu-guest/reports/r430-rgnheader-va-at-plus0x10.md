# r430: TA Header +0x10 = RgnHeader device VA (3-hop chain measured); r428 file attribution corrected

**Conclusion**: The render-target metadata VA at TA Header `+0x10` is the **RgnHeader device VA** —
proven by a 3-hop measured chain:
`local_5b0[1] = RgnHeader VA` (RGXAddRenderTarget:49312) →
`RTData entry+0x00` (SetupRTDataSet:48867) →
`TA_buf+0x10` (RGXPrepareTA:52144).
This round also corrects r428: `RGXAddRenderTargetDDK2` lives at
`linux-legacy-umd-5.2.0/decompiled.c:50203`, **not** in mtdxum64.dll.

## 1. Correction: r428 file misattribution [MEASURED]

- Corpus-wide grep for `MLIST`/`RgnHeader`: hits **only** in `linux-legacy-umd-5.2.0/`
  (`decompiled.c`, `symbols.jsonl`, `strings.jsonl`). Zero hits in `mtdxum64.dll`
  or any other decompiled directory.
- `RGXAddRenderTargetDDK2` = `linux-legacy-umd-5.2.0/decompiled.c:50203`,
  270 lines (50203–50472). r428's "270 lines" was correct; the file was wrong
  (line-number coincidence 50202/50203).
- `mtdxum64.dll/decompiled.c:50202` sits inside `FUN_180044870`, a C++
  container initializer (`_aligned_malloc(0x280,0x40)` + list-head init) —
  no render-target logic whatsoever.

## 2. MLIST allocation [MEASURED]

`DevmemAllocateAndMap` (`FUN_00196f30`) call sites:

| Item | Value |
|------|-------|
| Call | `FUN_00196f30(1, heap, numRT × config+0x5c, 0x80, 0x1000000103, "MLIST", &host_out, &dev_va)` |
| Size factor | `config+0x5c` = `0x4a000` (or `0x72000`), written by `RGXRenderTargetInitConfig:48828` (`*(param_3+0x5c) = uVar4`, uVar4 = 0x4a000/0x72000) |
| 64×64, 1 RT | `0x4a000` = 303,104 bytes |
| Purpose | Firmware-**written** macro-tile lists (UMD does not pre-fill; TA output target) |
| Variants | `"MLISTMcg"` (multicore), same size formula |

## 3. RgnHeader allocation [MEASURED]

| Item | Value |
|------|-------|
| Call | `FUN_00196f30(1, heap, numRT × round_up(tiles×0x40,64), 0x40, 0x133\|0x1000000103, "RgnHeader", &host_out, &dev_va)` |
| Size factor | `config+0x60` = `tilesX×tilesY×0x40` (`RGXRenderTargetInitConfig`: tilesX=(w+0x1f)>>5, tilesY=(h+0x1f)>>5) |
| 64×64, 1 RT | 4 tiles × 0x40 = `0x100` bytes (4 region headers, 64B each) |
| Init | UMD pre-fills every dword with `1` via `InitRegionHeaderBuffer` (maps host view with `FUN_001967a0`, loops `*p = 1`; conditional on heap=`0x133` path; error string at :49344/:49574) |
| Variants | `"RgnHeaderMcg"` (multicore) |

## 4. The 3-hop chain: TA+0x10 = RgnHeader VA [MEASURED]

All in `linux-legacy-umd-5.2.0/decompiled.c`:

1. **Hop 1** — `RGXAddRenderTarget:49312`: `local_5b0[1] = local_6d8`
   (`local_6d8` = RgnHeader **device VA** from `DevmemAllocateAndMap` out-param;
   host mapping goes to `puVar11[0]`, proving out-param order: the UMD writes `1`s
   through the host mapping while the VA is stored for firmware.)
2. **Hop 2** — `SetupRTDataSet:48867`:
   `*(undefined8 *)(param_2 + 0x38) = *(undefined8 *)(param_4 + 8)`
   i.e. `RTDataSet+0x38` (= first RTData entry `+0x00`, entry stride 0xD0) =
   `local_5b0[1]` = RgnHeader VA.
3. **Hop 3** — `RGXPrepareTA` (`FUN_00178800`):`52144`:
   `*(undefined8 *)(lVar8 + 0x10) = uVar10` where `lVar8` = TA buffer and
   `uVar10 = *(param_2+0x1c8) = *puVar14` = RTData entry `+0x00`.

Therefore **`psKickTA[1]` = RgnHeader device VA**. The firmware parses the
RgnHeader to learn the tile layout before running TA. Our r425 failure
(`+0x10` = raw 16KB pixel BO) is fully explained: firmware parsed pixel data
as region headers → garbage → hang.

## 5. Minimal valid TA Header (Linux-guest-constructible)

| Offset | Value | Construction | Status |
|--------|-------|--------------|--------|
| `+0x10` | RgnHeader dev VA | Allocate 0x100 B device-visible mem (64×64), fill `0xFFFFFFFF`, use its VA | [MEASURED] |
| `+0x28` | ? | `RTDataSet+0x440` (source struct unclear in disasm) | [UNKNOWN] |
| `+0x30` | ? | `RTDataSet+0x448` (source struct unclear in disasm) | [UNKNOWN] |
| `+0x68` | 0 | u32 (`(u32)((*ta_state & 3) == 3)`) | [MEASURED] |
| rest | 0 | — | [MEASURED] (r414) |

## 6. r431 prerequisites

- **P0**: Implement RgnHeader allocation in the Linux guest driver: device-visible
  0x100 B (for 64×64; formula `round_up(tiles×0x40,64)`), fill with `0xFFFFFFFF`,
  set `TA_buf+0x10` to its device VA. Keep `+0x28`/`+0x30` = 0 for the first attempt.
- **P1**: If firmware still times out, investigate `+0x28`/`+0x30`
  (candidates: MLIST VA, color-buffer VA — unconfirmed).
- **Gate**: T1–T5 must pass; recommend a new unit test asserting RgnHeader
  all-ones init and the `round_up(tiles×0x40,64)` size formula.

## 7. Honest boundaries

- `+0x28`/`+0x30` semantics [UNKNOWN]; their source (`RTDataSet+0x440`/`+0x448`)
  could not be traced to a concrete allocation in this round.
- RgnHeader per-dword semantics [UNKNOWN] (firmware-private); the all-ones
  init is UMD behavior [MEASURED], not a firmware requirement proof.
- MLIST contents [UNKNOWN] (firmware-written during TA).
- **Zero hardware touched, pure offline, no code changes.**
