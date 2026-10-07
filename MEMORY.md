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

## 本次会话进展（r260：ck 下 legacy blit，批准执行）

- ck（默认 major）下真实 blit 与 r244 同形：止于 `0x89:0x0` → -25，UMD 中止；ck 不干扰 `0x89` 路径。trace 落硬盘暂存区后已清空（与 r244 入库版同构）。拆桥干净，默认 + L3 全绿。详见 `reports/r260-ck-blit.md`。
- **Freeze 已恢复。**无代码改动。
- 遗留：GFX 重建；slices；TQX 真发射；CCB 解读；backend。USB 短页标题日期问题留待对应轮。
---
