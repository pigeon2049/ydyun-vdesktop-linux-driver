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
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](MEMORY-HISTORY-2026-10-08.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r301：落池首验像素仍差 + r300 设计，批准执行）

- r300 离线：fire-into-destination（work 验拷 + handler 等 fire 再 bump，60s 可中断）；门禁 +5；378+292 全绿，W=1 零警告。
- r301 活体：五开两发；首发尾块 span 修；复打执行落地（todst=1 全验）但 UMD 像素 FAIL（候选错池/stride/错色 → r302 离线判）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。
- **Freeze 已恢复。**
- 遗留：r302 池归属/stride/真色判定。本地提交仍未 push。
---

