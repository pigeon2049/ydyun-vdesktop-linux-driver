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

## 本次会话进展（r262：slices 活体未达预期，批准执行）

- 新构建（build-id 一致、调用存在）上机 + 真实 blit 后 `tqx-ctx: ready` 正常，但 slices 三种打印全无；已排除在载≠盘内/调用点错/dmesg 丢；调用未到达待查（下轮加打印验证）。其余路径正常。拆桥干净，默认 + L3 全绿。详见 `reports/r262-slices-noop.md`。
- **Freeze 已恢复。**教训：活体前先确认调用点可达的最小信号。
- 遗留：调用点打印验证；fire 函数；fired/verified。USB 短页标题日期问题留待对应轮。
---
