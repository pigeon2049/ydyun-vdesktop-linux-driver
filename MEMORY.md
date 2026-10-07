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

## 本轮进展（r292：同寿命双发全绿，批准执行）

- 第三窗口零干预：seq=1/seq=2 背靠背 `fired=1 chunks=21 verified=1`（单发复位成立）；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第四/五样本 nonzero=40（轮值第 10/11 值）。脚本多轮化（BLITS/WINID）。
- **Freeze 已恢复。**
- 遗留：TA/3D 或 update 的 UMD 驱动验证（需离线 recon producer；blit submit3 后 hanging 是前置山）。本地提交仍未 push。
---

