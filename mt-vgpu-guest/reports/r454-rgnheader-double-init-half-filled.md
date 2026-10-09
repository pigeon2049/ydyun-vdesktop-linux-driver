# r454：UMD RgnHeader 双循环初始化——我方只写了一半

**结论：UMD `InitRegionHeaderBuffer` 做两次循环写入（2×local_700 dwords），我方只做一次（64 dwords）。RgnHeader 初始化量只有 UMD 的一半。r453 的"未填充真实数据"假说被证伪——1s 就是完整初始化，问题是量不够。**

## 研究发现

### 1. UMD 初始化：双循环 [MEASURED]

`decompiled/linux-legacy-umd-5.2.0/decompiled.c`（`InitRegionHeaderBuffer` 内联逻辑）：

```c
iVar5 = FUN_001967a0(uVar13,local_690);  // Map RgnHeader to CPU
if (iVar5 == 0) {
    uVar2 = 0;
    local_700 = local_700 >> 2;  // bytes → dwords
    if (local_700 != 0) {
        do {  // 第一循环
            *local_690[0] = 1;
            uVar2 = uVar2 + 1;
            local_690[0] = local_690[0] + 1;
            uVar3 = 0;
        } while (local_700 != uVar2);
        do {  // 第二循环
            *local_690[0] = 1;
            uVar3 = uVar3 + 1;
            local_690[0] = local_690[0] + 1;
        } while (local_700 != uVar3);
    }
    FUN_001969c0(uVar13);  // Unmap
}
```

- 两个循环，每个写 `local_700` 个 dword 的 `1`
- 总计：**2 × local_700 dwords**

### 2. 我方初始化：单循环 [MEASURED]

`kernel/recovery/mt_pvr_bridge.c:4271-4272`：

```c
for (i = 0; i < MT_TA_RGNHEADER_BYTES / sizeof(u32); i++)
    rgn_dw[i] = MT_TA_RGNHEADER_INIT_DWORD;  // 0x1
```

- `MT_TA_RGNHEADER_BYTES = 0x100U`（256 字节）
- 写入：64 dwords

### 3. 差异：我方只写了一半

| | UMD | 我方 | 比例 |
|---|---|---|---|
| 循环次数 | 2 | 1 | — |
| 每循环 dwords | local_700 (≈64) | 64 | 相同 |
| 总 dwords | **≈128** | **64** | **我方 50%** |
| 总字节 | ≈512 | 256 | 我方 50% |

### 4. r453 假说被证伪

r453 提出："UMD 在 `InitRegionHeaderBuffer` 之后填入真实 region 数据，我们从未填充。"

**证伪**：`InitRegionHeaderBuffer` 内联代码中，两个 1s 循环之后直接 `FUN_001969c0`（unmap），**没有任何额外的数据写入**。1s 就是完整的初始化。

**修正后的根因**：不是"未填充真实数据"，而是"**初始化量不足**"——UMD 写 128 dwords，我方只写 64 dwords。

### 5. 0x100B vs 0x100 的澄清

- r430 报告 "64×64: 0x100B" —— **应为笔误**，实际是 `0x100`
- `kernel/mt_ta_real.h:120` 注释："64x64: 4 tiles * 0x40 = 0x100" —— 数学正确（4×64=256）
- `MT_TA_RGNHEADER_BYTES = 0x100U` —— **定义正确**，不是 typo
- r454 之前误以为 `0x100U` 是 `0x100B` 的笔误，**此判断错误**，特此纠正

### 6. 双循环的用途 [INFERRED]

UMD 分配：`uVar2 * (uVar4 & 0xffffffc0)` 字节，其中 `uVar2` 为 buffer 数量。

- 若 `uVar2=2`：分配 2 个 buffer，两个循环各初始化一个 → **双缓冲**（ping-pong）
- 若 `uVar2=1`：分配 1 个 buffer，两个循环写同一 buffer 的两半 → **单个大 buffer**

[UNKNOWN]：具体是哪种，需进一步分析 `uVar2` 的取值逻辑。

## 证据等级

- [MEASURED]：UMD 双循环（反汇编两处：49330-49338、49560-49568）
- [MEASURED]：我方单循环 64 dwords（`mt_pvr_bridge.c:4271-4272`）
- [MEASURED]：`InitRegionHeaderBuffer` 后无额外写入（反汇编）
- [INFERRED]：我方初始化量为 UMD 的 50%（基于循环次数对比）
- [UNKNOWN]：双循环是双缓冲还是单个大 buffer

## 下一步（r455 P0）

1. **修复初始化量**：将我方 RgnHeader 填充从 64 dwords 增加到 128 dwords（2×）
   - BO 已分配 4096 字节，空间充足
   - 只需修改填充循环的次数
2. **验证**：新增测试验证填充量为 128 dwords
3. **反向验证**：确认旧代码（64 dwords）在新测试上 FAIL

## 诚实边界

- 双循环的用途（双缓冲 vs 大 buffer）未完全确定，但"量不足"的结论不受影响
- `local_700` 的精确值未在 64×64 场景下实测，基于代码注释的 0x100 推断
- 本轮纯离线，未修改代码，未触碰硬件
