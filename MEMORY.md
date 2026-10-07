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

## 本次会话进展（r255：短预算 10 轮 soak，批准执行）

- 同窗口（wait 100ms）10/10 通过，0.12ms/轮起；tag/fence 无跳号（至 79/80）；零 WARN。拆桥干净，默认 + L3 全绿。详见 `reports/r255-tinybudget-soak.md`。
- **Freeze 已恢复。**无代码改动。

## 本次会话进展（r254：极小预算失配腿，批准执行）

- wait 100ms 下失配 kick 0.118s 后 UMD 37；预算维度全覆盖（5s/1s/100ms 同构）；无 marker。同窗口续跑 soak。详见 `reports/r254-tinymismatch.md` + `.jsonl`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
