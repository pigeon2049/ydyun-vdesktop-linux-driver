# r433: RgnHeader init is 0x00000001 not 0xFFFFFFFF; +0x28/+0x30 chain traced to stack-ambiguous source; MLIST not in kick path

> Round r433 (2026-10-09). **纯离线。** r432 RgnHeader 活体超时后的深挖。

## Conclusion

**三个发现：**

1. **RgnHeader 填充值是 `0x00000001`，不是 `0xFFFFFFFF`** — r430/r431 的 "0xFFFFFFFF" 结论错误。反汇编 `InitRegionHeaderBuffer` (`*local_690[0] = 1`) 逐 dword 写整数 `1`。我方 r431 的 `memset(0xFF)` (0xFFFFFFFF) 与 UMD 行为不符，**r434 必须修正**。

2. **`+0x28`/`+0x30` 链条完整追踪**：`TA_buf+0x28/+0x30` ← `TA_state+0x1cc/+0x1ce` ← `RTDataSet+0x440/+0x448` ← `*(local_5b0+0x68)`/`*(local_5b0+0x80)` (RGXAddRenderTarget)。终端值因 Ghidra 数组定界 [UNKNOWN]；MLIST VA 为首要候选。

3. **MLIST 不在 kick 路径中**：MLIST (0x4a000B, firmware-written) 的 VA 未出现在 TA Header 或 psKickTA 反汇编中。TA 提交可能不需要 MLIST VA，或经 `+0x28`/`+0x30` 传递。

## 1. RgnHeader per-dword: fill = 1 [MEASURED] [CORRECTS r430/r431]

### 反汇编证据

`linux-legacy-umd-5.2.0/decompiled.c`:

**声明** (49032):
```c
undefined4 *local_690 [4];  // dword* 数组
```

**填充** (49328/49334, 非 Mcg 路径; 49552/49562, Mcg 路径):
```c
*local_690[0] = 1;          // 逐 dword 写整数 1 = 0x00000001
local_690[0] = local_690[0] + 1;
```

**结论**: 每个 dword 被写入值 `1` (0x00000001)，**不是** `0xFFFFFFFF`。

### 对 r430/r431 的纠正

| 轮次 | 声称 | 状态 |
|------|------|------|
| r430 | "UMD pre-fills 0xFFFFFFFF via InitRegionHeaderBuffer" | ❌ 错误 |
| r431 | `MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFFU` + `memset(0xFF)` | ❌ 与 UMD 行为不符 |
| r433 | `*local_690[0] = 1` → 0x00000001 | ✅ [MEASURED] |

**r431 注释** `#define MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFFU /* InitRegionHeaderBuffer fills 1s */` 中的 "fills 1s" 被误读为"全 1 比特"；实际是"填充值 1"。

### Per-dword patching (Mcg 路径)

多 RT 时，填充后特定 dword 被 patch (49653+):
```c
local_690[0][2]  = local_690[0][2] & 0x1f | (uint)local_318 & 0xfffffff0;
local_690[0][3]  = local_318._4_4_;   // VA 高 32 位
// stride 0x40 dwords: [0x42]/[0x43], [0x82]/[0x83], ...
```

单 RT (我方 64x64) 无 patching，仅 fill。

### r434 前置 (P0)

将 `MT_TA_RGNHEADER_INIT_DWORD` 改为 `0x00000001U`，`mt_render_context_create()` 改用 dword 循环写 `1` (或 `memset` 后 patch——不，memset 只能设字节；需逐 dword 写)。

## 2. +0x28/+0x30 完整链条 [MEASURED, 终端 UNKNOWN]

### 链条

```
TA_buf+0x28 = *(TA_state+0x1cc) = *(RTDataSet+0x440) = *(local_5b0+0x68)
TA_buf+0x30 = *(TA_state+0x1ce) = *(RTDataSet+0x448) = *(local_5b0+0x80)
```

**证据**:
- `RGXPrepareTA` (52100-52103): `param_2+0x1cc = *(lVar3+0x440)`, `param_2+0x1ce = *(lVar3+0x448)` (lVar3=RTDataSet)
- `RGXPrepareTA` (52144+): `TA_buf+0x28 = *(param_2+0x1cc)`, `TA_buf+0x30 = *(param_2+0x1ce)`
- `SetupRTDataSet` (48953): `param_2+0x440 = *(param_4+0x68)`, `param_2+0x448 = *(param_4+0x80)` (param_4=local_5b0)

### 终端值 [UNKNOWN]

`local_5b0` 在 `RGXAddRenderTarget` 声明为 `long [11]` (88B)，`+0x68` (104B) / `+0x80` (128B) 超出定界。Ghidra 数组定界不可靠；实际栈布局未知。

**候选** (按可能性):
1. **MLIST VA** — MLIST 在 RgnHeader 之前分配 (`local_6d0`→`local_6d8`→`local_558`)，VA 在相邻栈区
2. **零** — 未初始化栈
3. 其他缓冲 VA

### psKickTA 对应

- `psKickTA[3]` = TA_buf+0x28
- `psKickTA[10]` = TA_buf+0x30

## 3. MLIST: firmware-written,不在 kick 路径 [MEASURED]

### 分配

`RGXAddRenderTarget` (49217):
```c
FUN_00196f30(1, local_6e0, uVar2 * local_5d4, 0x80, 0x1000000103, "MLIST", puVar11+1, &local_6d0);
// 64x64: 0x4a000 = 303,104 bytes
// local_6d0 = MLIST dev VA → local_6d8 → local_558 (first RT)
```

### 关键结论

- MLIST VA (`local_558`/`local_550`) 在分配后**未见后续引用** (grep 无读取)。
- MLIST 未出现在 TA Header 写入 (`RGXPrepareTA`) 或 psKickTA 构建 (`FUN_0017d890`) 中。
- r430: "Firmware-written macro-tile lists (UMD does not pre-fill; TA output target)" — UMD 分配但不预填，固件在 TA 执行时写入。

**推论**: TA kick 可能不需要 MLIST VA (固件从其他结构派生)，或经 `+0x28`/`+0x30` 传递 (待验证)。

## 4. r434 前置清单

### P0 (必须)
1. **RgnHeader fill 改为 `0x00000001`**: `MT_TA_RGNHEADER_INIT_DWORD` → `0x1U`; `mt_render_context_create()` 逐 dword 写 1 (替代 `memset(0xFF)`)。
2. 更新 C 测试 `test_ta_rgnheader_init_pattern` (期望 0x00000001)。

### P1 (候选)
3. `+0x28`/`+0x30` 尝试 MLIST VA (需先实现 MLIST 分配) 或保持 0。
4. 若仍超时，考虑 RgnHeader per-dword patching (单 RT 可能不需要)。

### Gate
- T1-T5 必须通过；新增测试断言 fill=1。

## 5. Honest boundaries

- RgnHeader `=1` 为 [MEASURED] (两处 fill 点一致)；固件是否要求此值 [INFERRED] (UMD 行为，非固件规范)。
- `+0x28`/`+0x30` 终端值 [UNKNOWN] (Ghidra 定界限制)；MLIST VA 为候选 [INFERRED]。
- MLIST 在 kick 路径中的作用 [UNKNOWN]；"不需要"为 [INFERRED] (未在反汇编中找到引用)。
- **本轮零硬件触碰，纯离线，无代码变更。**
