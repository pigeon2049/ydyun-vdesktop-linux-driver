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

## 本轮进展（r324：返值抓取未遂，批准执行）

- finish 版脚本空跑（pending 未命中，9 行）；r323 收敛为“全进入”；两步走方案已定。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：r325 两步走读返值。已推送。
---

## 本轮进展（r323：setup 三元组全进入（r324 已收敛“全返回”待证），批准执行）

- BlitInit/CheckFences/LookUpEOT 全进入全返回（含参数）；abort 在下游，RGXTDMSubmit 未达；dprintf 文件脚本法定稿。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：LookUpEOT 返回值/出参（断点停机读 rax 或静态跟分支）。已推送。
---

