# r411：真实 TA 包构造基础设施落地（离线）

> 轮次：r411（2026-10-09）。**纯离线，零硬件触碰。**
> 背景：r410 前置清单（P1 DM 布局/P1 DMA+VA/P2 最小条目）；用户指示"继续"。

## 结论

**真实 TA 包构造基础设施已落地，门控默认关闭，零行为变更。**

1. **新头文件** `kernel/mt_ta_real.h`（99 行）：`MT_TA_REAL_PACKET` 门控（默认 0）、`MT_TA_CMD_BUFFER_BYTES=0x168`、40B 条目结构、`mt_ta_entry_simple_build()`（Q2 维度打包 [MEASURED]）。
2. **`mt_marker_fence.h` 集成**：新增 `mt_fw_ta_real_command()`（#if 门控内），`mt_ta_submit_build` 按门控分发（marker vs real）。
3. **DM 布局**：VA @+0x28/+0x2c、size @+0x30，**[INFERRED] by 3D analogy (r381)**，TO-VALIDATE。门控关闭时零影响。
4. **新测试** `tests/ta/test_ta_real.py`（6 tests）：门控默认关、尺寸常量、条目构建器存在、real 命令被门控、marker 路径保留。

## 设计

### DM 包布局（P1）

| 偏移 | 字段 | 来源 |
|---|---|---|
| +0x0c | opcode 0x66 | [MEASURED] r365 |
| +0x28/+0x2c | TA buffer VA (u64) | [INFERRED] 3D analogy r381 |
| +0x30 | TA buffer size | [INFERRED] 3D analogy r381 |
| +0x48 | wire_id | [MEASURED] r365 |
| +0x4c | pid | [MEASURED] r365 |

**诚实边界**：TA 的 VA/size 偏移无直接证据，系 3D 类比推断。门控默认关闭，该布局在活体验证前不生效。

### 条目构造（P2）

`mt_ta_entry_simple_build(e, w, h)`：
- Q2 = `((w-1)&0x7fff)<<0x29 | ((h-1)&0x7fff)<<0x1a` [MEASURED] (r410, FUN_00169240:44304)
- Q4 = `w*h-1`
- Q0/Q1/Q3 = 0 [INFERRED] minimal

### 360B 缓冲管理（P1）

**未实现**：DMA 分配与 GPU VA 映射需生产路径改动（`dma_alloc_coherent` + R5/R6 VM 绑定），留待门控开启前的独立轮次。本轮仅完成包格式与条目构造基础设施。

## 变更点

| 文件 | 变更 |
|---|---|
| `kernel/mt_ta_real.h` | **新**：门控、常量、条目结构、构建器 |
| `kernel/mt_marker_fence.h` | +include；+`mt_fw_ta_real_command`（#if 内）；`mt_ta_submit_build` 门控分发 |
| `tests/ta/test_ta_real.py` | **新**：6 tests |

## 门禁

- `make -C mt-vgpu-guest check-offline`：**480 Python + 299 C 全绿**（474+6 新）
- `make kernel` W=1：**零警告**（门控开/关双路径验证；开路径仅卡 intentional guard assert）
- 反向验证：门控篡改为 1 → `test_gate_default_off` **FAIL**；还原后全绿

## 交付物

- 本报告 `reports/r411-ta-real-packet-infra.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r411（r409 按 §4 归档）
- `PROGRESS-SNAPSHOT.md` §12 追加 r411

## 诚实边界

- 纯离线，零硬件触碰；生产代码改动但门控关闭，零行为变更
- DM 布局为推断，需活体验证
- 360B DMA/VA 映射未实现（独立前置）
- 未 push（等用户指令）
