EXIT:0
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
> r425 轮按 §4 清理：r421 节已移入归档。

## r425 (2026-10-09): Header-only 活体——固件仍超时（Header-only 不充分）

r425（最高风险活体）：r423 Header-only 方案首次活体验证。双门控测试构建（MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1，static_assert 临时中和；userspace n_entries 1→0 临时；事后全部 revert；W=1 零警告）。pre-live T1-T5 全过（550+1416）。冷重启后 probe 全参数链加载，trial 重建成功（connect=0 pinned=1）；双门控桥加载，/dev/dri/renderD128 就绪。mt-ta-readback 全链路执行：context 0x1000，12th target BO（va=0x7b000000）绑定成功；0xFD 提交（Header-only：buf+0x10=target_va，其余零，n_entries=0 被接受）→ fence 分配 → 5s 无完成（-ETIMEDOUT）。结论：Header-only 不充分——r422 的"Entry 污染 Header"是真实 bug（T5 已拦截）但不是超时的完整解释；r414 全零="无工作"快路径。固件很可能要求 +0x10 指向 render-target 元数据结构（非原始像素 BO）及/或其他 Header 字段有效。pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f），待用户冷重启（第 5 次）。dmesg 零 WARN/BUG/Oops。门禁 550+1416 全绿。诚实边界：Header-only 仍 [INFERRED] 未 [MEASURED]；下一步必须离线确定 +0x10 真实语义与必需 Header 字段；不再做无离线依据的活体试探。

## r422 (2026-10-09): TA 缓冲 Header+Entries 双区——Entry 写错位置致 r421 超时（离线反汇编）

r422（纯离线，零硬件）：r421 超时根因定位。反汇编证实：RGXSubmitTA（FUN_001796b0，decompiled.c:54365）从 TA_buf+0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 回读 9 qword 到 psKickTA [MEASURED]；RGXPrepareTA（FUN_00178800）向 TA_buf+0x10/0x28/0x30/0x68/0x120/0x140 写 Header，覆盖 0x00-0x160 [MEASURED]。我方 `mt_ta_real_buffer_build()` 把 40B Entry 写在 `buf+0x00`（mt_ta_real.h:178-179），Q2/Q3/Q4（0x10-0x27）恰好覆盖 Header 的 0x10/0x18/0x20 字段 → 固件经 psKickTA 读到垃圾 Header → 挂起超时。r414 全零缓冲=空 Header=无工作，故 219us 成功；r421 非零 Entry=污染 Header，故超时。Q1=target_va（Entry 内）未被证伪，但 Entry 位置错误是更直接原因。Entries 真实容器未知（544B 缓冲为推断）。r423 前置：P0 确定 Entries 容器或 Header-only 测试（仅设 TA_buf+0x10=target_va）；P1 改 `mt_ta_real_buffer_build()` 不再写 buf+0；门禁 T5（Header 完整性）待加。诚实边界：Ghidra 伪 C 或有 artifact；本轮无代码变更。

