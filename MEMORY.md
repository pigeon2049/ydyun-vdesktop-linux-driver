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

## 本轮进展（r349：T2-b，离线 fabricated）

- `SubmitTA` 内逐个被调者断点：`SyncPrimRef` 首报 3（`INVALID_PARAMS`，零 handle 被拒）；T2-c 回填 tuple。零硬件触碰，freeze 继续。
- 遗留：T2-c 真 handle 回填（脚本三行，fabricated）。
---

## 本轮进展（r348：T2-a，离线 fabricated）

- 新脚本可复现 fabricated 双映射试探：`RGXKickTA -> 3`，`PrepareTA=0`，3 来自 `SubmitTA`；`0x82:0x14` 未发出。整形三跳收敛。
- 落库 `scripts/ta-kick-attempt1.sh`；r194“返回3”平反（系 SubmitTA 非守卫）。
- 遗留：T2-b SubmitTA 的 3 归因（`0x9c250`？）。
---







