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
> r412 轮按 §4 清理：r410 节已移入归档。
> r414 轮按 §4 清理：r411 节已移入归档。
> r415 轮按 §4 清理：r412 节已移入归档。
> r417 轮按 §4 清理：r414 节已移入归档。
> r418 轮按 §4 清理：r415 节已移入归档。
> r419 轮按 §4 清理：r419 节补入（前轮遗漏）。
> r420 轮按 §4 清理：r418、r417 节已移入归档。
> r421 轮按 §4 清理：r419、r418、r417 节已移入归档。

## r421 (2026-10-09): Q0 修正后活体——提交成功但固件仍超时（Q1 待深挖）

r421（最高风险活体）：双门控测试构建（MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1，static_assert 临时中和，事后 revert；W=1 零警告）。pre-live T1/T2/T3/T4 全过（13 tests）。冷重启后 probe 全参数链加载，trial 重建成功（connect=0 pinned=1）；双门控桥加载，/dev/dri/renderD128 就绪。mt-ta-readback 全链路执行：context 0x1000 创建，11 BO + 12th target BO（va=0x7b000000 bytes=16384）绑定成功；0xFD 提交（Q0=0x48000000000 flags-only [MEASURED]，Q1=0x7b000000 [INFERRED]）→ fence 分配 → 5s 无完成事件（-ETIMEDOUT，submitted-but-ignored）。对比 r414（Q0=0/Q1=0，219us 完成）：修正后的 Q0/Q1 编码仍未被固件接受。dmesg 零 WARN/BUG/Oops；pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f），待用户冷重启。门禁 543+299 全绿；kernel W=1 零警告；源码已 revert（仅保留 committed 状态），工作区干净。诚实边界：Q1=target_va 仍 [INFERRED]，本次活体未能将其提升为 [MEASURED]——Q1 编码可能仍不对，或 TA 条目其他字段（Q2/Q3/Q4）/DM 包布局另有问题；T2 像素回读仍 open；下一步 P0：离线深挖 Q1/target 语义（RGXPrepareTA 回读偏移 0x10/0x18/.../0x60 的对应关系）。

## r420 (2026-10-09): Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）

r420（离线，零硬件）：用户指示"继续 加更多测试和门禁"。新增 21 Python 测试：`tests/ta/test_q0_purity.py`（T4 门禁，3 tests：Q0 禁止 OR/address 源码扫描、常量仅 bits 39/42、Q1 必须接 VA）、`tests/ta/test_q0_q1_bitfields.py`（15 tests：Q0 flag 位独立、低 32 位禁区、Q1 48 位 mask 边界、三态历史 r414/r418/r419）、`tests/ta/test_ta_real.py::TestTaDmLayoutUsage`（3 tests：常量被使用、禁硬编码 0x28/0x30、注释 [MEASURED]）。修复 1 处 stale 文档：`mt_marker_fence.h` 的 [INFERRED] 注释更新为 [MEASURED]（r414）。T4 反向验证：注入 r418 污染 → FAIL（定位行号）；还原 → 绿。门禁 543+299 全绿（Python，1 skipped）、630 C 全绿；`make kernel` W=1 零警告。本地提交未 push。诚实边界：Q0 flag 语义（除 29/30/39/42）仍未知；Q1=target 待活体验。
