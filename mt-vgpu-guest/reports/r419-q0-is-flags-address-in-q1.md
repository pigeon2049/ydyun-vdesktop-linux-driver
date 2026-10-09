# r419：Q0 是纯 flags——地址在 Q1，r418 把 VA 塞错了位置

> 轮次：r419（2026-10-09）。**纯离线反汇编。**
> 背景：r418 活体 Q0=`va|0x48000000000` 导致固件超时；r414 Q0=0 曾 219µs 成功。

## 结论

**Q0 不含地址。Q0 是纯 flags/control 字；48 位目标地址在 Q1。**

r416/r418 的根本错误：把 `target_va | 0x48000000000` 塞进 Q0，用地址位污染了 flags，
固件看到垃圾 flags 后 hang（submitted-but-ignored）。

修正公式：
```c
Q0 = 0x48000000000ULL;              /* flags only, 无地址 */
Q1 = target_va & 0xFFFFFFFFFFFFULL; /* 48 位目标地址 */
```

## 反汇编证据（decompiled.c）

### Q0 初始构造（line 44213，FUN_00169240）
```c
local_90 = (ulong)(ushort)(sVar10 << 4) << 0x30 | 0x2000000000000000;
```
- bits 48–63 = `(sVar10<<4) & 0xFFFF`（sVar10 ∈ {1,4}，小整数）
- bit 61 = 1
- **无地址位**。Q0 从诞生起就是 flags。

### Q0 Path B（line 44317）
```c
local_90 = uVar15 | uVar17 & 0xfff8001f9fffffff | 0x48000000000;
```
即 `uVar15 | (prev_Q0 & 0xfff8001f9fffffff) | 0x48000000000`：
- `uVar15` = bit29 (`*(lVar29+0x88)!=0`) | bit30 (`param_1[0x30]!=0`) | bits43–50 (`local_c4`)
- mask `0xfff8001f9fffffff` 清除 bits {29,30,37–50} 后重填
- `0x48000000000` = bits **39** 和 **42**（非 43/46，r410 笔误纠正）
- **依然无地址**：prev_Q0 的低 37 位被完整保留（carry-forward），从未 OR 入地址。

### Q1 才是地址（lines 44300/44321）
```c
uStack_88._0_6_ = (undefined6)*(undefined8 *)(param_1 + 0x10);  /* 低 48 位 = 地址 */
uStack_88._7_1_ = (undefined1)(uVar17 >> 0x38);                 /* 顶字节 = prev Q0 bits56–63 */
uStack_88._0_7_ = CONCAT16((byte)(uVar17 >> 0x30) & 0xfe | (byte)param_1[0x18] & 1, ...);
```
- Q1 低 48 位 = `*(param_1 + 0x10)`：**48 位设备地址**
- Q1 bits 48–55 = prev Q0 bits 48–55（bit48 被 `param_1[0x18]&1` 替换）
- Q1 bits 56–63 = prev Q0 bits 56–63

### Q3 也有地址（lVar29+8，56 位）
```c
uStack_78._0_7_ = CONCAT16(bVar9 | *(byte *)(lVar29 + 0x28) & 1, (int6)*(undefined8 *)(lVar29 + 8));
```

### 写回确认（5-qword / 9-qword）
```c
*puVar3 = local_90;   /* Q0 */
puVar3[1] = uStack_88; /* Q1 = 地址 */
puVar3[2] = local_80;  /* Q2 = 维度 */
puVar3[3] = uStack_78; /* Q3 = 地址2 */
puVar3[4] = local_70;  /* Q4 */
```

## Q0 位域分解表

| Bits | 含义 | 来源 | 状态 |
|------|------|------|------|
| 0–28 | flags（Path A 做字节级 surgery） | local_90 carry | [MEASURED] 结构，语义 [INFERRED] |
| 29 | `*(lVar29+0x88)!=0` | uVar15 | [MEASURED] |
| 30 | `param_1[0x30]!=0` | uVar15 | [MEASURED] |
| 31–38 | 保留（carry） | prev Q0 | [MEASURED] |
| 39, 42 | 置 1（Path B） | `0x48000000000` | [MEASURED] |
| 43–50 | `local_c4`（FUN_001bae00 产物） | uVar15 | [MEASURED] 存在，语义 [INFERRED] |
| 48–63 | `(sVar10<<4)`，bit61 置 1 | 初始构造 | [MEASURED] |
| — | **无地址位** | — | [MEASURED] ✅ |

## r414 vs r418 对比

| | r414（成功，219µs） | r418（超时） |
|---|---|---|
| Q0 | `0` | `va \| 0x48000000000` |
| Q1 | `0` | `0` |
| 结果 | 固件接受 | 固件 hang |

r414 的 Q0=0 是"干净的 flags"（全零 flags 被容忍）；r418 把 48 位 VA OR 进 Q0 低位，
污染了 bits 0–28 的 flags 区，固件无法解析 → 超时。**Q1 在两轮中都是 0**，
说明固件容忍零地址（dummy 无真实几何），但不容忍 flags 污染。

## 修正

`mt_ta_entry_simple_set_target()`（`kernel/mt_ta_real.h:127`）：
```c
/* r419: Q0 是纯 flags（FUN_00169240:44213/44317），地址在 Q1（:44300）。
 * 切勿把 VA OR 进 Q0（r418 教训）。 */
e->q0_addr_flags = MT_TA_ENTRY_Q0_FLAG_BITS;   /* 0x48000000000，flags only */
e->q1 = target_va & 0xFFFFFFFFFFFFULL;          /* 48 位目标地址进 Q1 */
```

## 诚实边界

- Q1 = render target 的推断基于"Q1 含 48 位地址 + TA state 语义"，未做活体验
- Q0 各 flag 位的具体语义（除 29/30/39/42 外）仍未知；`0` 被 r414 证明可接受
- `q0_addr_flags` 字段名现已名不副实（无 addr），留待重命名（不属本轮）
- 本轮纯离线；r420 活体验证修正公式

## 门禁

- `make -C mt-vgpu-guest check-offline`：全绿
- `make kernel` W=1：零警告
