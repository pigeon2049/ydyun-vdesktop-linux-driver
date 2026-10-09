# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](memory/MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](memory/MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](memory/MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](memory/MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](memory/MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](memory/MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](memory/MEMORY-HISTORY-2026-10-08.md)。
> r363 轮按 §4 清理：r361 节已移入归档。
> r364 轮按 §4 清理：r362 节已移入归档。
> r365 轮按 §4 清理：r363 节已移入归档。
> r368 轮按 §4 清理：r364 节已移入归档。
> r369 轮按 §4 清理：r365 节已移入归档。
> r370 轮按 §4 清理：r366 节已移入归档。
> r372 轮按 §4 清理：r367 节已移入归档。
> r373 轮按 §4 清理：r368 节已移入归档。
> r378 轮按 §4 清理：r376 节已移入归档。
> r374 轮按 §4 清理：r369 节已移入归档。
> r379 轮按 §4 清理：r377 节已移入归档。
> r380 轮按 §4 清理：r378 节已移入归档。
> r382 轮按 §4 清理：r379 节已移入归档。
> r383 轮按 §4 清理：r380 节已移入归档。
> r384 轮按 §4 清理：r381 节已移入归档。
> r385 轮按 §4 清理：r382 节已移入归档。
> r386 轮按 §4 清理：r385 节已移入归档。
> r387 轮按 §4 清理：r383 节已移入归档。
> r388 轮按 §4 清理：r386、r384 节已移入归档。
> r390 轮按 §4 清理：r387 节已移入归档。
> r391 轮按 §4 清理：r388 节已移入归档。
> r392 轮按 §4 清理：r389 节已移入归档。
> r393 轮按 §4 清理：r390 节已移入归档。
> r394 轮按 §4 清理：r391、r392 节已移入归档。
> r395 轮按 §4 清理：r393 节已移入归档。
> r396 轮按 §4 清理：r394 节已移入归档。
> r398 轮按 §4 清理：r395 节已移入归档。
> r402 轮按 §4 清理：r400、r399 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。
> r404 轮按 \u00a74 清理：r402、r401 节已移入归档。
> r405 轮按 §4 清理：无（仅 r404、r405 两节，保留）。
> r407 轮按 §4 清理：r405 节已移入归档。
> r408 轮按 §4 清理：r406 节已移入归档。
> r409 轮按 §4 清理：r407 节已移入归档。
> r410 轮按 §4 清理：r408 节已移入归档。


## r411 (2026-10-09): 真实 TA 包构造基础设施（离线）

- 新 `kernel/mt_ta_real.h`（99 行）：`MT_TA_REAL_PACKET` 门控默认 0、`MT_TA_CMD_BUFFER_BYTES=0x168`、40B 条目结构、`mt_ta_entry_simple_build()`（Q2 维度打包 [MEASURED] r410）。
- `mt_marker_fence.h` 集成：`mt_fw_ta_real_command()`（#if 门控内，VA @+0x28/size @+0x30 系 3D 类比 [INFERRED] TO-VALIDATE）；`mt_ta_submit_build` 门控分发 marker vs real。
- 新 `tests/ta/test_ta_real.py`（6 tests）：门控默认关、尺寸常量、条目构建器、real 命令被门控、marker 保留。
- 360B DMA/VA 映射未实现（需生产路径改动，独立前置）。
- 门禁 480+299 全绿，`make kernel` W=1 零警告（门控开/关双验证）；反向验证通过（门控篡改→FAIL）。
- 纯离线，零硬件触碰；门控关闭零行为变更。本地提交未 push。

## r410 (2026-10-09): Windows 驱动挖掘——TA ISA 字段语义（离线）

**Windows 驱动**：/opt/MTT-driver-only/ 为纯二进制（24 DLL/SYS，无头文件/文档）；TA ISA 语义来自 Linux UMD 反汇编。

**TA 缓冲字段语义**（FUN_00169240，decompiled.c:43159）：40B（5 qwords）简单条目 / 72B（9 qwords）复杂条目；render_ctx+0xb6 指针推进。
- Q0 (local_90)：地址/标志，uVar16 位打包；或 0x48000000000 标志
- Q1 (uStack_88)：*(param_1+0x10)；byte7 标志位
- Q2 (local_80)：打包维度 ((w-1)&0x7fff)<<0x29 | ((h-1)&0x7fff)<<0x1a
- Q3 (uStack_78)：*(lVar29+8)；byte6 标志位
- Q4 (local_70)：维度乘积或打包维度
- Q5-Q8（复杂）：scissor/viewport 坐标打包

**RGXPrepareTA 回读验证**：psKickTA 字段从缓冲偏移 0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 读取，确认布局。

**Linux 桥差异**：当前仅发 80B marker；缺 360B VA/size/DMA 映射/条目构造/psKickTA 结构。

**前置条件**：P1 确认固件包布局 + DMA 映射；P2 最小条目构造；P3 回读验证（可选）。

报告 mt-vgpu-guest/reports/r410-windows-ta-isa.md。

