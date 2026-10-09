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
> r428 轮按 §4 清理：r426 节已移入归档。
> r429 轮按 §4 清理：r427 节已移入归档。
> r430 轮按 §4 清理：r428 节已移入归档。
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

> r427 轮按 §4 清理：r425 节已移入归档。
> r431 轮按 §4 清理：r429 节已移入归档。
> r432 轮按 §4 清理：r430 节已移入归档。
> r433 轮按 §4 清理：r431 节已移入归档。

## r433 (2026-10-09): RgnHeader 填充是 0x00000001 非 0xFFFFFFFF;+0x28/+0x30 链条追踪（离线）

- **核心纠正**：`InitRegionHeaderBuffer` 逐 dword 写整数 `1` (`*local_690[0] = 1`,
  `undefined4*` [MEASURED])，**不是** `0xFFFFFFFF`。r430/r431 的 "0xFFFFFFFF"
  结论错误；r431 `MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFFU` + `memset(0xFF)` 与 UMD
  行为不符。**r434 P0: 改为逐 dword 写 `0x00000001`。**
- **+0x28/+0x30 链条** [MEASURED, 终端 UNKNOWN]：
  `TA_buf+0x28/+0x30` ← `TA_state+0x1cc/+0x1ce` ← `RTDataSet+0x440/+0x448`
  ← `*(local_5b0+0x68)`/`*(local_5b0+0x80)` (RGXAddRenderTarget)。
  终端值因 Ghidra 数组定界 [UNKNOWN]；MLIST VA 为首要候选 [INFERRED]。
- **MLIST** [MEASURED]：0x4a000B (64x64)，firmware-written，UMD 不预填；
  VA (`local_558`) 分配后未见引用；未出现在 TA Header/psKickTA 中。
  TA kick 可能不需要 MLIST VA，或经 +0x28/+0x30 传递。
- **Mcg patching**：多 RT 时填充后 patch `[2]/[3]` (VA 低/高 32 位，stride 0x40 dwords)；
  单 RT (我方) 无 patching，仅 fill。
- 门禁 `check-offline` 全绿；`make kernel` 未跑（无代码变更）。
  零硬件触碰，纯离线。

## r432 (2026-10-09): RgnHeader 活体——固件仍超时，RgnHeader 非充分条件（最高风险）

- 核心结论：RgnHeader BO 正常创建绑定（va=0x7c000000 bytes=4096，0xFF 预填），
  TA Header +0x10 正确指向 RgnHeader，但固件 5s 内仍无完成（-ETIMEDOUT）。
  RgnHeader 是必要非充分条件；+0x28/+0x30 或 RgnHeader 内容语义仍有缺失。
- 活体：双门控测试构建（W=1 零警告）；T1-T5 全过（554 Python + 1490 C）；
  第 5 次冷重启后 trial 重建（connect=0 pinned=1）；0xFD 提交走通（fence 已分配）；
  仅完成事件缺失。dmesg 零 WARN/BUG/Oops。
- Teardown：pending fence 导致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f）；
  待用户第 6 次冷重启。源码已 revert，默认门控重建零警告，工作区干净。
- 对比表：r414 全零→219us（无工作快路径）；r425 +0x10=像素 BO→超时；
  r432 +0x10=RgnHeader→仍超时。RgnHeader [INFERRED] 未升 [MEASURED]（证伪性证据）。
- 下一步必须离线：+0x28/+0x30 语义与 RgnHeader per-dword 要求；
  不再做无依据活体试探。零 rmmod -f、零自行重启。
