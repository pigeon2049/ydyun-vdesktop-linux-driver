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


## r431 (2026-10-09): RgnHeader 13th BO 实现——TA Header +0x10 指向 RgnHeader（离线）

- **核心结论**：render context 新增第 13 个 BO（RgnHeader）：64×64 → 0x100B，
  预填全 `0xFFFFFFFF`（[MEASURED] r430，InitRegionHeaderBuffer），绑定 VA slot 12
 （0x7c000000）。`TA_buf+0x10` 改取 `rgnheader_va`（不再是 16KB 像素 BO）；
  r425 超时（像素当 region header 解析）此路径不再重演。
- **实现**：`mt_ta_real.h` 新增常量 + `mt_ta_rgnheader_size(w,h)`（round_up(tiles×0x40,64)）；
  `mt_render_context.h` struct += 3 字段（sizeof 1720→1824）；`mt_pvr_bridge.c`：
  create 分配+0xFF 初始化+slot 12 绑定，destroy 释放，0x82:0xFD 要求 rgnheader_ready。
- **测试**：C 新增 size 公式（64×64→0x100、128×128→0x400、65×65→0x240）+
  全 1 初始化验证；Python layout 测试更新偏移。
- 门禁 `check-offline` **550 Python + 1490 C 全绿**；`make kernel` W=1 **零警告**。
- `+0x28`/`+0x30` 仍 [UNKNOWN]（置零）；活体验收延至 r432。零硬件触碰，纯离线。

