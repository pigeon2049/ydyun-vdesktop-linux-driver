# r438: +0x68 boolean + single-RT full RGXPrepareTA write list — new suspects: +0x50/+0x58 tile packing

> Round r438 (2026-10-09). **纯离线。** r437 终局（+0x28/+0x30 单 RT 恒 0、MLIST 不进 kick）后的深挖：
> `+0x68` 布尔语义 + 单 RT 下 `RGXPrepareTA` 完整写入清单 vs 我方 Header-only 逐项对照。

## Conclusion

**三个发现（[MEASURED] 反汇编，`linux-legacy-umd-5.2.0/decompiled.c`）：**

1. **`+0x68` = `(uint)((*param_2 & 3) == 3)`**（4B，`RGXPrepareTA:52136`）——`param_2`
   为 psKickTA（kick-TA state，`RGXKickTA`/`RGXKickGfx` 透传），`*param_2` 为其
   flags dword，bit0+bit1 全置才写 1。**flags 由 DDK 层设置，UMD 不写；
   真实提交中该位是 0 还是 1，UMD 反汇编无法确定 [UNKNOWN]**。我方写 0；
   若 DDK 置位则为 mismatch。
2. **单 RT 完整写入清单**（`FUN_00178800`，lVar8=TA_buf）：除 `+0x10`/`+0x28`/`+0x30`
   外，UMD 还写 **`+0x50`/`+0x58`（tile/dimension 打包）**、**`+0x68`（布尔）**、
   **`+0x120`（flags 位打包）**、`+0x138`–`+0x160`（feature 条件）；`+0x78` 起的
   多 RT 块在单 RT（`*(lVar6+0x18)==1`）下跳过。我方 Header-only 仅写 `+0x10`，
   其余全 0。
3. **最强新嫌疑：`+0x50`/`+0x58`** — `FUN_00184220` 将
   `((psKickTA[3 or 4]+0x3f)>>6 & 0x3f)` 打包进 bit48–53（`(x+63)/64` 即 tile
   数语义）。我方写 0 = "0 tiles"，固件可能据此判定 render 区域非法而挂起。

## 1. +0x68 布尔语义 [MEASURED 表达式，UNKNOWN 取值]

### 写入点

`decompiled.c:52136`（`FUN_00178800` = `RGXPrepareTA`）：

```c
*(uint *)(lVar8 + 0x68) = (uint)((*param_2 & 3) == 3);
```

同函数 52357 处（第二 TA_buf 路径，lVar4）为逐字相同的写入：
`*(uint *)(lVar4 + 0x68) = (uint)((*param_2 & 3) == 3);`

### param_2 身份 [MEASURED]

- 函数签名：`FUN_00178800(undefined8 param_1, uint *param_2, long *param_3)`。
- 调用点：`RGXKickTA:53215`（`FUN_00178800(param_1,param_2)`，param_2 即
  `RGXKickTA` 的 param_2）；`RGXKickGfx:54811`
  （`FUN_00178800(param_1,param_2,local_eb8+3)`，param_2 校验失败时报错
  `"psKickTA invalid"`）。
- **结论：`param_2` = psKickTA（kick-TA state 结构体指针），由 DDK/client 层
  创建并传入；`*param_2`（首 dword）为其 flags。**

### flags bit 用法（RGXPrepareTA 内）[MEASURED]

| bit | 用法 | 位置 |
|---|---|---|
| 0+1 | `+0x68 = ((flags & 3) == 3)` | 52136 |
| 0 或 3 | `+0x120` bit1 置位 `(uVar5 & 9)` | 52213 |
| 3 | `+0x120` bit8 `((flags & 8) << 5)` | 52202 |
| 4+5 | 全置 → `FUN_00178320` early-out（跳过 TA_buf 写入） | 52107–52116 |
| 12/13/17/19/24/25 | `+0x120` 各 bit 打包 | 52204–52222 |

### 取值 [UNKNOWN]（诚实边界）

psKickTA 由 DDK 层分配初始化，UMD 反汇编语料内无其构造点
（`RGXKickTA`/`RGXKickGfx` 均为顶层导出函数）。因此**真实 TA 提交中
bit0+bit1 是否置位，无法从 UMD 反汇编确定**。`+0x68` 在我方 Header-only
中为 0；若 DDK 置位则 mismatch，为本轮嫌疑之一（P1）。

## 2. 单 RT 完整写入清单 vs Header-only [MEASURED]

`lVar8` = TA_buf（360B）。单 RT 条件：`*(lVar6+0x18) == 1`
（RT 计数，r437 [INFERRED] 高置信）→ `+0x78` 起多 RT 块整体跳过
（`(*(lVar6+0x18) != 1)` 为假）。

| 偏移 | 大小 | UMD 写入（单 RT） | 我方 Header-only | 对照 |
|---|---|---|---|---|
| +0x10 | 8B | RgnHeader VA（uVar10） | RgnHeader VA | ✅ 一致 |
| +0x28 | 8B | 0（r437 [MEASURED]） | 0 | ✅ 一致 |
| +0x30 | 8B | 0（r437 [MEASURED]） | 0 | ✅ 一致 |
| +0x50 | 8B | `((psKickTA[3]+0x3f>>6)&0x3f)<<48`（FUN_00184220） | 0 | ❌ **mismatch（P0 嫌疑）** |
| +0x58 | 8B | `((psKickTA[4]+0x3f>>6)&0x3f)<<48`（FUN_00184220） | 0 | ❌ **mismatch（P0 嫌疑）** |
| +0x68 | 4B | `(uint)((*psKickTA & 3) == 3)` | 0 | ⚠️ P1 嫌疑（取值 UNKNOWN） |
| +0x120 | 4B | flags 位打包（`+0x120`=0 起，OR 入 `*param_2` 各 bit） | 0 | ⚠️ P2（r414 全零可完成"无工作"） |
| +0x138 | 4B | `*(lVar6+0x10)`（feature 条件） | 0 | ?（feature 值未知） |
| +0x140 | 4B | `puVar2[0x171]` 或 0（feature 条件） | 0 | ? |
| +0x148 | 4B | `*(lVar6+0x18)`（feature 条件） | 0 | ? |
| +0x150 | 4B | `*(lVar6+0x1c)` 或 0（feature 条件） | 0 | ? |
| +0x158 | 4B | 0（else 分支） | 0 | ✅ 一致 |
| +0x160 | 4B | 0（else 分支） | 0 | ✅ 一致 |
| +0x78… | 8B | 跳过（单 RT） | 0 | ✅ 一致 |

### FUN_00184220（tile 打包）[MEASURED]（decompiled.c:58215）

```c
void FUN_00184220(int param_1, uint param_2, ulong *param_3)
{
  if (param_2 < 2) {
    *param_3 = *param_3 | ((ulong)(param_1 + 0x3fU >> 6) & 0x3f) << 0x30;
    return;
  }
  ...
}
```

- `RGXPrepareTA:52192`：`FUN_00184220(uVar13, 0, lVar8+0x50)`，
  `uVar13 = param_2[3]`（psKickTA+0xc）。
- `RGXPrepareTA:52193`：`FUN_00184220(uVar4, 1, lVar8+0x58)`，
  `uVar4 = param_2[4]`（psKickTA+0x10）。
- `+0x50` 事先 16B 清零（52191），故 `+0x50` =
  `((psKickTA[3]+63)/64 & 0x3f) << 48`，`+0x58` =
  `((psKickTA[4]+63)/64 & 0x3f) << 48`。
- **语义 [INFERRED]**：`(x+63)/64` 为 tile 数（64 像素/tile）；bit48–53
  极可能是 render 目标宽/高的 tile 计数。64×64 → 1，即 bit48 置位。
  我方写 0 = "0 tiles"——固件可能判定 render 区域非法而挂起不完成。

## 3. 对我方实现的意义

**嫌疑优先级（更新 r437 名单）：**

1. **P0：`+0x50`/`+0x58` tile 打包** — UMD 必写（无条件，单 RT 路径内），
   我方为 0。`(x+63)/64` 的 tile 语义强；0 tiles  plausibly 致固件拒绝。
   r439 应实现：`+0x50`/`+0x58` 按 `(dim+63)/64` 填 bit48–53
   （dim 取自 w/h；psKickTA[3]/[4] 的 DDK 语义未知，用 w/h 近似并标
   [INFERRED]）。
2. **P1：`+0x68` 布尔** — 取值 UNKNOWN；若 DDK 置位则 mismatch。
   可在 r439 一并试验置 1（低风险：4B 布尔），或先保持 0 观察 P0 效果。
3. **P2：`+0x120` flags** — r414 全零可走"无工作"快路径；真实提交的
   flags 组合未知。P0/P1 无效后再议。
4. **P3：`+0x138`–`+0x160` feature 条件块** — `lVar6 = GetFeatures()`（无参）
   的值未知；若目标机 feature 使条件为真，则 UMD 写非零而我方为 0。
   需先确定 `GetFeatures` 返回（离线可查，但 feature 值与固件版本相关）。

## 4. r439 前置（P0，离线→实现）

1. 在 `mt_ta_real_buffer_build()`（Header-only）中新增 `+0x50`/`+0x58`
   写入：`((u64)(((w+63)/64) & 0x3f) << 48)` 与 h 对应值，标 [INFERRED]
   （tile 语义）/[MEASURED]（打包公式、bit 位置）。
2. 同步更新 T5 白名单（`test_header_integrity.py`）：`+0x50`/`+0x58`
   加入允许写入集合（当前仅 `MT_TA_BUF_HDR_TARGET_VA`=0x10）。
3. 新增 C/Python 测试：tile 打包公式（64×64→bit48=1；128×64→w 侧 2；
   65×65→2）。
4. `+0x68` 暂保持 0（P1 待 P0 活体验收后决定）；`+0x120` 保持 0。
5. **P0 实现+门禁全绿后，方可活体**（r380 起教训延续）。

## 5. Honest boundaries

- `+0x68` 取值 [UNKNOWN]（DDK 层行为，UMD 语料无构造点）。
- `+0x50`/`+0x58` 的 tile 语义 [INFERRED]；打包公式与 bit 位置 [MEASURED]；
  `psKickTA[3]`/`[4]` 的 DDK 语义 [UNKNOWN]（用 w/h 近似为工程折中）。
- `+0x138`–`+0x160` 的 feature 条件取值未知；单 RT 下 `lVar6` 的实际
  内容未验证。
- **本轮零硬件触碰，纯离线，无代码变更。**
