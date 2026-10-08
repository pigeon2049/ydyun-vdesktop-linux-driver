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

## 本轮进展（r350：T2-c，离线 fabricated）

- `SyncPrimRef` 要非空描述子，传入 NULL 即 3；真 handle 已落 `b10`，回填位置未中。零硬件触碰，freeze 继续。
- 脚本增量已单提交；mapB 漏 `$SYNC` 已补。
- 遗留：T2-d 描述子选中步骤。
---

## 本轮进展（r349：T2-b，离线 fabricated）

- `SubmitTA` 内逐个被调者断点：`SyncPrimRef` 首报 3（`INVALID_PARAMS`，零 handle 被拒）；T2-c 回填 tuple。零硬件触碰，freeze 继续。
- 遗留：T2-c 真 handle 回填（脚本三行，fabricated）。
---








