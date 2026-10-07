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

## 本轮进展（r304：CCB 扫描离线 + r303 归属反转补记，零硬件触碰）

- r303 补记：池清单三池同尺寸 nz=0/1/2621440——fire 在填源池（0x1032），目的池（0x1019）恒零；归属反转实锤。
- r304：官方树无执行逻辑可抄（OS 胶水 + 闭二进制，RE 采矿链见报告）；CCB 目的扫描（共享头 + C 回环自测，`+40` 以执行纠正）+ locate 定向（force/启发双模）；门禁；380+299 全绿，W=1 零警告。未加载。
- 遗留：r305 定向 fire 窗口（真实绘制像素闭环候选）。本地提交仍未 push。
---

## 本轮进展（r301：落池首验像素仍差 + r300 设计，批准执行）

- r300 离线：fire-into-destination（work 验拷 + handler 等 fire 再 bump，60s 可中断）；门禁 +5；378+292 全绿，W=1 零警告。
- r301 活体：五开两发；首发尾块 span 修；复打执行落地（todst=1 全验）但 UMD 像素 FAIL（候选错池/stride/错色 → r302 离线判）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。
- **Freeze 已恢复。**
- 遗留：r302 池归属/stride/真色判定。本地提交仍未 push。
---

