# r437: +0x28/+0x30 are zero by UMD design (single-RT); MLIST VA never enters kick path

> Round r437 (2026-10-09). **纯离线。** r436 RgnHeader fill-1 活体超时后的深挖：验证 r433 的 MLIST VA 候选假说。

## Conclusion

**两个终局性发现（[MEASURED]）：**

1. **`+0x28`/`+0x30` 在单 RT 下恒为 0** — 不是 MLIST VA，不是缺失字段。我方置零与 UMD 行为完全一致，**r432/r436 的超时与这两个字段无关**。
2. **MLIST VA 分配后从未被 kick 路径引用** — r433 的"首要候选 [INFERRED]"被证伪。

## 1. +0x28/+0x30 终端解析 [MEASURED]（终结 r433 的 UNKNOWN）

### 栈槽恒等式

`RGXAddRenderTarget` (`linux-legacy-umd-5.2.0/decompiled.c:49044`): `long local_5b0 [11]`，位于 rbp-0x5b0。Ghidra 按栈偏移命名局部变量：

- `local_5b0 + 0x68` = rbp-0x5b0+0x68 = rbp-0x548 = **`local_548`**
- `local_5b0 + 0x80` = rbp-0x5b0+0x80 = rbp-0x530 = **`local_530`**

### 赋值点（全函数唯一）

decompiled.c:49349-49350，位于 `if (local_62c < 2)`（单 RT）分支内：

```c
local_530 = 0;
local_548 = 0;
```

49000–50000 全范围 grep：`local_548`/`local_530` **无其他赋值点**；`local_5b0` 数组写入仅 `[0]`–`[3]`（+0x00–+0x18），无越界写入 `[13]`（=+0x68）/`[16]`（=+0x80）的可能。`local_62c` 为 ushort（RT 计数语义 [INFERRED]，高置信：`< 2` 分支 vs Mcg 多 RT 路径）；我方单 RT（64×64）走该分支。

### 链条闭合

```
TA_buf+0x28 = *(local_5b0+0x68) = local_548 = 0   [MEASURED]
TA_buf+0x30 = *(local_5b0+0x80) = local_530 = 0   [MEASURED]
```

（经 `SetupRTDataSet:48953-48954` → `RGXPrepareTA:52144-52145`，中间链 r433 已验证。）

psKickTA 对应：`[3]` = TA_buf+0x28 = 0，`[10]` = TA_buf+0x30 = 0（单 RT）。

## 2. MLIST VA 去向 [MEASURED]

decompiled.c:49216-49222：

```c
uVar3 = FUN_00196f30(1,local_6e0,uVar2 * local_5d4,0x80,0x1000000103,"MLIST",puVar11 + 1,
                     &local_6d0);
if (uVar3 == 0) {
  local_6d8 = local_6d0;
  if (uVar2 != 0) {
    local_558 = local_6d0;   // MLIST dev VA（首 RT）——全函数唯一赋值点
```

49000–49900 全范围 grep：`local_558` **零读取**。MLIST VA 分配后即被丢弃：不在 TA Header 写入（`RGXPrepareTA`）中，不在 psKickTA 构建（`FUN_0017d890`）中。固件要么从 RgnHeader 或其他结构派生 MLIST，要么在 kick 时根本不需要它。

## 3. 对我方实现的意义

| 字段 | UMD 行为（单 RT） | 我方 Header-only | 一致 |
|---|---|---|---|
| +0x10 | RgnHeader VA | RgnHeader VA (0x7c000000) | ✅ [MEASURED] |
| +0x28 | 0 | 0 | ✅ [MEASURED] |
| +0x30 | 0 | 0 | ✅ [MEASURED] |
| +0x68 | `(uint)((*param_2 & 3) == 3)`（布尔，4B） | 0 | ⚠️ 待确认 |

**r432/r436 超时的嫌疑名单更新**：`+0x28`/`+0x30` 移除；MLIST VA 移除。

## 4. r438 前置（P0，离线）

剩余嫌疑（按优先级）：

1. **`+0x68` 布尔**：UMD 写 `(*param_2 & 3) == 3`（`RGXPrepareTA:52136`）；我方写 0。若真实 TA 的 `param_2`（TA state）flags 使该位为 1，则我方 Header 与 UMD 不一致。需确定真实提交中 `param_2` 的 flags。
2. **单 RT 下 RGXPrepareTA 完整写入清单**：按 feature/RT 数条件写入的字段（+0x78/+0x80/+0xb0/+0xe8/+0x120/+0x140/+0x150/+0x160…）；列出单 RT 基础路径的**全部**写入并与我方 Header-only 逐项对照。
3. **RgnHeader 内容**：fill=1 已被 r436 证伪为根因；per-dword 是否需非均匀内容仍未知。
4. **DM 提交包本身**：若 Header 已与 UMD 一致仍超时，问题可能在 DM3/0x66 envelope（VA/size/flags），而非 TA 缓冲内容。

**P0 完成前不得活体**（r380/r418/r421/r425/r432/r436 教训延续）。

## 5. Honest boundaries

- 栈槽恒等式依赖 Ghidra `local_<hex>` = rbp-`<hex>` 命名约定（反汇编器既定语义）。
- `local_62c` 的 RT 计数语义为 [INFERRED]（高置信）；Mcg/多 RT 路径下 +0x28/+0x30 可能非零——本结论仅限单 RT。
- **本轮零硬件触碰，纯离线，无代码变更。**
