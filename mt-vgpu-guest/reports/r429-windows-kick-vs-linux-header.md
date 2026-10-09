# r429: Windows driver TA submission uses 0x78-byte kick, not 360B Header

**Conclusion**: mtdxum64.dll (DX10/11 UMD) does NOT use a 360B TA Header. It builds
0x78-byte kick entries (magic 0x3089705f3089705f at index [1]; Linux UMD has it at
[4]) submitted via D3DDDI. The 360B TA Header is Linux-UMD-specific. The Windows
driver proves firmware accepts multiple submission formats.

## 1. Kick structure builders ([MEASURED])

### FUN_180224220 (decompiled.c:392802)
0x78 bytes per entry, 15 qwords zeroed then filled:

| Index | Value | Source |
|-------|-------|--------|
| [0] | VA | FUN_1802176e0(param_1+0x9fa8) |
| [1] | 0x3089705f3089705f | magic, UMD-written |
| [2] | 0x100000000 | constant |
| [3] | 2 | constant |
| [4] | flags | conditional uVar7 OR 0x100 |
| [5] | 0 / 0x30110 | version-conditional |
| [6] | 0x200 / 0xe200 | version-conditional |
| [8] | format code | pixel format map (2->1, 4->5, 8->7) |
| [9] | packed dims | ((h-1 and 0x7fff) << 16) OR (w-1 and 0x7fff) |
| [10] | VA masked | ctx+0x85a0 masked fffffffffffffff0 |
| [0xb] | VA>>4 | ctx+0x85a8 shifted right 4 |
| [0xc]/[0xd] | 0 or 0x70/0x40/0x1c0 | conditional |
| [0xe] | 0 or computed | conditional |

### FUN_1802411e0 (decompiled.c:412745)
Variant builder; magic also at [1]; [2]=0x101/0x103; [3]=0x30110; [7]=packed dims.

Caller: FUN_18021cf50 builds a 0x78-stride array, zeroes 15 qwords, then calls
FUN_180224220 per entry (line 388440).

## 2. Linux UMD vs Windows delta

| Item | Linux UMD 5.2.0 | Windows mtdxum64.dll |
|------|-----------------|----------------------|
| Submit unit | 360B TA buffer + psKickTA[18] | 0x78B kick entry array |
| magic index | psKickTA[4] | kick[1] |
| Header concept | yes (0x00-0x160) | no (D3D11 model) |
| Submit path | RGXSubmitTA to bridge | D3DDDIEscapeCb to KMD |

## 3. 0x168 anchor denoising

Of 151 hits in mtdxum64.dll:
- ~140 are vtable offsets (+0x168 struct offset) -- noise
- ~8 are C++ object sizes (destructors) -- noise
- ~3 have real 360B semantics -- all Linux-UMD-side; no Windows TA Header alloc

Conclusion: Windows D3D11 UMD does not allocate a 360B TA Header.

## 4. Meaning for Linux guest driver

1. Cannot copy directly: Windows kick format is incompatible with Linux TA Header
   (different abstraction layers).
2. Firmware accepts multiple formats: same firmware accepts D3D11 kicks and Linux
   TA Headers -- format negotiated by UMD/KMD, not firmware-hardcoded.
3. r427 correction stands: UMD allocates firmware-visible structs (MLIST/RgnHeader
   on Linux side); layout recoverable from disassembly.
4. Next: Linux-side RGXAddRenderTargetDDK2 MLIST/RgnHeader layout remains the best
   lead for render-target metadata (r429 work item 3 deferred to r430).

## 5. Honest boundaries

- Kick field semantics [INFERRED] (naming/context); magic position [MEASURED].
- Windows-side render-target metadata allocation not located (stripped names).
- Zero hardware touched, pure offline; no code changes.
