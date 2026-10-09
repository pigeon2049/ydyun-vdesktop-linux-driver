# r409：真实 TA 路径缺口确认——标准程序不可达，TA 缓冲结构已解析

> 轮次：r409（2026-10-09）。**离线为主 + 标准程序实测。**
> 背景：r408 发现 RGXSubmitTA 不构造 TA 缓冲；用户指示"随便找个测试程序"。

## 结论

**三条路径验证完毕，真实 TA 活体需 r410 专项：**

1. **标准程序**：不可行（实测确认）
2. **TA 缓冲结构**：已解析（40B/72B 条目序列）
3. **真实 TA 包**：需改包构建器（含 360B VA），r410 做

## 1. 标准程序实测（预期失败，实证）

| 程序 | 结果 | 原因 |
|---|---|---|
| `egltri_x11` | 运行 8s，0 次 musakickgfx2 dispatch | Mesa 走 llvmpipe，无 DDK2 UMD 后端 |
| `glxinfo` | OpenGL renderer: llvmpipe (LLVM 19.1.7) | 软件渲染，不走 0x82 桥 |

**机制**：Mesa 无 DDK2 UMD 后端；标准程序走 DRM 通用 ioctl，不发 0x82:0xC。
X server 存在（:0），但程序选 llvmpipe 而非 pvr 设备。

## 2. TA 缓冲结构解析（离线反汇编）

**来源**：`decompiled/linux-legacy-umd-5.2.0/decompiled.c`，`FUN_00169240`

**填充逻辑**（render_ctx+0xb6 指针推进）：

- 简单条目：5 qwords = 40B = 0x28
  - `[local_90, uStack_88, local_80, uStack_78, local_70]`
  - 指针 += 0x28
- 复杂条目：9 qwords = 72B = 0x48
  - 上面 5 个 + `[uStack_68, local_60, uStack_58, local_50]`
  - 指针 += 0x48

循环 `while (local_d4 < param_2[0xe])` 逐条目填充，值由渲染状态计算
（lVar29+0x68/0x6c/0x74 等维度字段经位打包）。

**调用链**：

```
FUN_00169240 (填充 360B TA state)
  → RGXKickTA (0x17afd0)
    → RGXPrepareTA (FUN_00178800): param_3[0] = *(render_ctx+0xb6)
    → RGXSubmitTA (FUN_001796b0): 透传 VA, size=0x168
      → BridgeRGXKickTA3D3 (FUN_00137180)
```

**关键术语**："TA state buffer"
（字符串 "AllocateMemory: Unable to allocate TA state buffer (%d)"）。

**r408 结论 corroborate**：
- `lVar23 = *param_3` (decompiled.c:52614) —— VA 直接透传
- `-(uint)(lVar23 != 0) & 0x168` —— 360 为立即数尺寸
- RGXSubmitTA 内零写入 —— 只透传，不构造

## 3. 真实 TA 包缺口（r410 前置工作）

**当前路径**（`kernel/mt_marker_fence.h` `mt_ta_submit_build`）：

```c
mt_fw_ta_marker_command(packet, wire_id, pid);  // 仅 80B marker
```

**不含** ta_cmd_va / ta_cmd_size —— `ta_params` 只存不发。
当前 TA 路径为 marker-only，与 r372/r373 的 0x100 完成一致。

**r410 需做**：

1. 扩展包构建器：在 DM 包中含 360B 缓冲 VA + size 字段
   （需确认固件期望的包布局 —— 80B 之后追加？还是独立描述符？）
2. 360B 缓冲的 DMA 可见 VA 映射（`dma_alloc_coherent` 或 VM 绑定）
3. 条目内容语义：各 qword 含义需进一步反汇编 `FUN_00169240` 的输入
   （local_90 等由何种渲染状态计算）

## 交付物

- 本报告 `reports/r409-ta-path-gap-confirmed.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r409
- `PROGRESS-SNAPSHOT.md` §12 追加 r409

## 门禁

- Pre-live T1/T2/T3：全过（10 tests OK，活体前）
- `make -C mt-vgpu-guest check-offline`：474 Python + 299 C 全绿
- `make kernel` W=1：零警告（无内核改动）
- 零桥操作（标准程序运行不触桥）

## 诚实边界

- 未做 TA 真实包活体（需生产路径改动，不宜凌晨一次性测试）
- 360B 各 qword 语义未知（只知结构，不知值）
- 标准程序测试为预期内失败，非阻塞性发现
- 反汇编为 Ghidra 伪 C，可能有 decompiler artifact
