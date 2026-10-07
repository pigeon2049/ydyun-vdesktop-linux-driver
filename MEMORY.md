# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本次会话进展（r259：r210 配方可复现性审计，零硬件触碰）

- 开工声明零硬件触碰（fabricated + 硬盘暂存区）。结论：r210 无完整 harness 命令归档（GDB 全裸 run、history 无记录），关键缺 UMD 对象指针来源（GDB 手动读址填址未归档），不可直接复现；最小 GFX 命令 GDB 下返回 3（r194 同形复现成功）。暂存区已清空。详见 `reports/r259-gfx-repro-audit.md`。
- 未改码、未跑门禁、未碰会话。
- 遗留：完整重建（多轮）；真桥重放；TQX 真发射；CCB 解读；backend。USB 短页标题日期问题留待对应轮。
---
