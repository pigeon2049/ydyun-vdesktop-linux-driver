# r422：TA 缓冲是 Header+Entries 双区结构——我们的 Entry 写错了位置

> 轮次：r422（2026-10-09）。**纯离线反汇编。** r421 超时的根因定位。
> 背景：r421 Q0 干净+Q1=target 仍超时；r414 全零缓冲 219µs 成功。决定不再无依据活体试探。

## 结论

**360B TA 缓冲是 Header（0x00–0x160+）+ Entries 双区结构；我们的代码把 40B Entry 写在 `buf+0x00`，恰好覆盖了 UMD Header 的关键字段（0x10/0x18/0x20），导致固件解析到垃圾 Header 而挂起。Q1=target_va 的 [INFERRED] 仍未被证伪，但 Entry 位置错误是更直接的超时原因。**

## 反汇编证据

### 1. RGXSubmitTA 从 Header 回读（decompiled.c:54365-54374，[MEASURED]）

`FUN_001796b0`（RGXSubmitTA）从 TA 缓冲（`*(param_2+0xb6)`）读取 9 个 qword 到 psKickTA：

| TA_buf 偏移 | → psKickTA 槽位 | 备注 |
|---|---|---|
| +0x10 | [1] (+0x08) | 8B |
| +0x18 | [6] (+0x30) | 8B |
| +0x20 | [7] (+0x38) | 8B |
| +0x28 | [3] (+0x18) | 8B |
| +0x30 | [10] (+0x50) | 8B |
| +0x38 | [0xb] (+0x58) | 8B |
| +0x40 | [9] (+0x48) | 8B |
| +0x48 | [8] (+0x40) | 8B |
| +0x60 | +0x74 | 4B |

### 2. RGXPrepareTA 写 Header（FUN_00178800，[MEASURED]）

`lVar8 = *(param_2+0xb6)`（TA 缓冲基址），写入：

- `lVar8+0x10` = uVar10（render target VA，来自 ctx 缓冲数组）
- `lVar8+0x28` = `*(param_2+0x1cc)`
- `lVar8+0x30` = `*(param_2+0x1ce)`
- `lVar8+0x68/0x78/0xb0/0xe8`（8B VA/指针）
- `lVar8+0x120/0x140/0x150/0x160`（4B flags）

**Header 至少覆盖 0x00–0x160，360B 缓冲的绝大部分是 Header，不是 Entries。**

### 3. 我们的代码写错了位置（[MEASURED]，mt_ta_real.h:178-179）

```c
struct mt_ta_entry_simple *e =
    (struct mt_ta_entry_simple *)(buf + i * MT_TA_ENTRY_SIMPLE_BYTES);
// Entry 0 → buf+0x00，40B 覆盖 0x00–0x27
```

40B Entry 的 Q2（0x10–0x17）、Q3（0x18–0x1F）、Q4（0x20–0x27）**恰好覆盖** UMD Header 在 0x10/0x18/0x20 的字段（SubmitTA 会回读到 psKickTA[1]/[6]/[7]）。

### 4. 超时机制解释

| 轮次 | TA_buf+0x10/0x18/0x20 | 固件行为 |
|---|---|---|
| r414 | 0x00（全零缓冲） | Header 全零 → "无工作" → 219µs 完成 |
| r421 | 我们的 Q2/Q3/Q4（非零 dummy） | Header 非零 → 尝试解析 → 垃圾值 → 挂起超时 |

**r414 的成功不是"Q0=0 被容忍"，而是"全零 Header 被识别为空工作"。r421 的失败不是"Q1 编码错"，而是"Header 被 Entry 数据污染"。**

## Q1 语义结论

- **Q1=target_va（Entry 内）**：仍 [INFERRED]，**未被证伪也未被证实**。r421 的超时不能作为 Q1 错误的证据，因为 Header 污染是更直接的原因。
- **固件的 render target**：来自 TA 缓冲 **Header**（0x10/0x28 等字段，经 psKickTA 传递），**不是** Entry 的 Q1。Entry Q1（`*(param_1+0x10)`，r419）是每-draw 的地址，语义仍未知。
- **Entries 的真实位置**：不在 360B Header 缓冲内。544B 缓冲（psKickTA[2]，`*(param_2+0xba)`）是更可能的 Entries 容器。[INFERRED，需验证]

## Q2/Q3/Q4 审查

- r421 的 Q2（64×64 维度打包）本身格式正确（r419 [MEASURED]）。
- 但 Q2/Q3/Q4 被写在了 **Header 区域**（0x10/0x18/0x20），固件按 Header 语义解读 → 垃圾 → 挂起。
- **不是** Q2/Q3/Q4 的值问题，是**位置**问题。

## r423 前置条件清单

1. **P0**：确定 Entries 的正确容器（544B 缓冲？还是 360B 内 Header 之后的偏移？）。需继续反汇编 `FUN_00169240` 的目标指针来源。
2. **P0**：r423 测试方案（二选一）：
   - **方案 A（Header-only，最小风险）**：360B 全零，仅设 `TA_buf+0x10 = target_va`（Header 字段，→psKickTA[1]），零 Entries。验证固件是否完成且 Header 被正确读取。
   - **方案 B（Entries 异地）**：找到 Entries 正确偏移后，在非 Header 区写 Entry。
3. **P1**：`mt_ta_real_buffer_build()` 必须改：Entry 不再写 `buf+0`；Header 字段单独设置。
4. **门禁**：新增 T5（Header 完整性门禁）：`buf+0x00–0x68` 范围内禁止写 Entry 数据（源码扫描）。

## 诚实边界

- Entries 真实位置仍未知（544B 缓冲为推断）。
- Q1=target_va（Entry 内）的语义仍 [INFERRED]。
- 本轮零硬件触碰，纯离线反汇编；Ghidra 伪 C 或有 decompiler artifact。
- r423 在 P0 完成前不得活体。

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮纯分析，无代码变更）
- 本轮无代码变更，无需 `make kernel`

## 交付物

- 本报告 `reports/r422-ta-buffer-header-vs-entries.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r422（§4 清理）
- `PROGRESS-SNAPSHOT.md` §12 追加 r422
- 本地提交（不 push）
