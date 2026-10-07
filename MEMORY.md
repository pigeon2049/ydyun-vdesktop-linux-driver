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

## 本次会话进展（r243：短预算 UMD 全链匹配，批准执行）

- 短预算下 UMD 全链非零匹配 0.046s 即过（`0x2:0xa` 预置 + kick 全 0，fence=40）；预算不影响命中路径。拆桥干净，默认 + L3 全绿。详见 `reports/r243-shortbudget-umdmatch.md` + `.jsonl`。
- **Freeze 已恢复。**无代码改动。

## 本次会话进展（r242：DDK2 短预算失配，批准执行）

- `=2` + wait 1s 下 DDK2 失配 1.008s 后 UMD 37；major×预算双正交；无 marker。同窗口换参数续跑 r243。详见 `reports/r242-ddk2short-mismatch.md` + `.jsonl`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
