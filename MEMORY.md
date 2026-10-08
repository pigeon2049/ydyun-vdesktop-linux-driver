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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r345：app 未描述 surface，离线）

- app copy 序列调用清点：无 surface 描述调用；`CreateCCB` 只 calloc。r333–r345 因果链闭合，copy 线关账（重心回 fill 扩展或 TA，由用户拍板）。零硬件触碰，freeze 继续。
- 遗留：vendor 完整 copy 流程对照（开放项）。
---

## 本轮进展（r344：分发门，离线）

- 反汇编 `RGXTDMQueueTransferNew`：特性门 `>1→TQJobSubmit`（尾跳）、`≤1→legacy`；`rdx+8` 由 r342×r338 活体互证；`=2`/默认行为分裂得解。ctx 空壳随 job 传入，候选出自 `CreateTransferContext`。零硬件触碰，freeze 继续。
- 遗留：`CreateTransferContext` 建表契约 + fill 序列对照（离线）。
---



