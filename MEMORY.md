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
> r422 轮按 §4 清理：r420 节已移入归档。
> r408 轮按 §4 清理：r406 节已移入归档。
> r409 轮按 §4 清理：r407 节已移入归档。
> r410 轮按 §4 清理：r408 节已移入归档。
> r412 轮按 §4 清理：r410 节已移入归档。
> r414 轮按 §4 清理：r411 节已移入归档。
> r415 轮按 §4 清理：r412 节已移入归档。
> r417 轮按 §4 清理：r414 节已移入归档。
> r418 轮按 §4 清理：r415 节已移入归档。
> r419 轮按 §4 清理：r419 节补入（前轮遗漏）。
> r420 轮按 §4 清理：r418、r417 节已移入归档。
> r421 轮按 §4 清理：r419、r418、r417 节已移入归档。

## r422 (2026-10-09): TA 缓冲 Header+Entries 双区——Entry 写错位置致 r421 超时（离线反汇编）

r422（纯离线，零硬件）：r421 超时根因定位。反汇编证实：RGXSubmitTA（FUN_001796b0，decompiled.c:54365）从 TA_buf+0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 回读 9 qword 到 psKickTA [MEASURED]；RGXPrepareTA（FUN_00178800）向 TA_buf+0x10/0x28/0x30/0x68/0x120/0x140 写 Header，覆盖 0x00-0x160 [MEASURED]。我方 `mt_ta_real_buffer_build()` 把 40B Entry 写在 `buf+0x00`（mt_ta_real.h:178-179），Q2/Q3/Q4（0x10-0x27）恰好覆盖 Header 的 0x10/0x18/0x20 字段 → 固件经 psKickTA 读到垃圾 Header → 挂起超时。r414 全零缓冲=空 Header=无工作，故 219us 成功；r421 非零 Entry=污染 Header，故超时。Q1=target_va（Entry 内）未被证伪，但 Entry 位置错误是更直接原因。Entries 真实容器未知（544B 缓冲为推断）。r423 前置：P0 确定 Entries 容器或 Header-only 测试（仅设 TA_buf+0x10=target_va）；P1 改 `mt_ta_real_buffer_build()` 不再写 buf+0；门禁 T5（Header 完整性）待加。诚实边界：Ghidra 伪 C 或有 artifact；本轮无代码变更。

## r421 (2026-10-09): Q0 修正后活体——提交成功但固件仍超时（Q1 待深挖）

r421（最高风险活体）：双门控测试构建（MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1，static_assert 临时中和，事后 revert；W=1 零警告）。pre-live T1/T2/T3/T4 全过（13 tests）。冷重启后 probe 全参数链加载，trial 重建成功（connect=0 pinned=1）；双门控桥加载，/dev/dri/renderD128 就绪。mt-ta-readback 全链路执行：context 0x1000 创建，11 BO + 12th target BO（va=0x7b000000 bytes=16384）绑定成功；0xFD 提交（Q0=0x48000000000 flags-only [MEASURED]，Q1=0x7b000000 [INFERRED]）→ fence 分配 → 5s 无完成事件（-ETIMEDOUT，submitted-but-ignored）。对比 r414（Q0=0/Q1=0，219us 完成）：修正后的 Q0/Q1 编码仍未被固件接受。dmesg 零 WARN/BUG/Oops；pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f），待用户冷重启。门禁 543+299 全绿；kernel W=1 零警告；源码已 revert（仅保留 committed 状态），工作区干净。诚实边界：Q1=target_va 仍 [INFERRED]，本次活体未能将其提升为 [MEASURED]——Q1 编码可能仍不对，或 TA 条目其他字段（Q2/Q3/Q4）/DM 包布局另有问题；T2 像素回读仍 open；下一步 P0：离线深挖 Q1/target 语义（RGXPrepareTA 回读偏移 0x10/0x18/.../0x60 的对应关系）。

