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

## 本次会话进展（r249：observer 在 transfer 桥下回归，批准执行）

- observer 与 `translate_transfer` 开关正交证实：`=2` + transfer 下 24 步全过，CCB 行一致；拆桥干净，默认 + L3 全绿。无代码改动。详见 `reports/r249-transfer-observe-regression.md`。
- **Freeze 已恢复。**

## 本次会话进展（r248：EBUSY 后重试，批准执行）

- 循环并发第 2 轮复现一胜一败（同 r247 形态）；败者链单独重跑全过（0.18ms，translator 常驻）；拒绝无副作用、可恢复。同窗口续跑 r249。详见 `reports/r248-ebusy-retry.md`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
