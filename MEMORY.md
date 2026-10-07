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

最后更新：2026-10-07（r202 update-list 生命周期核对）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r202：update-list 生命周期核对）

- 零硬件触碰。`FUN_00178800` 按 count 分配 update-list 并复制条目；RGXKickGfx 成功路径经 fabricated bridge 后 free 该块。SubmissionDestroy 释放 region descriptor，但不是 update-list；helper-only 重放只证明 descriptor 所有权，排除其为 r201 free 槽的直接重复释放。
- 证据：`reports/r202-update-list-lifetime.md`。下一步 GDB 追踪 update-list 块边界、调用前后元数据及此前写入者。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r201：fabricated GFX update producer 动态复核）

- 零硬件触碰。GDB 对上 kick `+0x28` 目标 `+0x200` 与 render-context allocator；修正 `CreateSyncPrim` 输出槽后，CheckSync/UpdateSync 各一项，update `flag=2`、handle 非空，trace 发出 fabricated `0x82:0x14`（IN108/OUT4）。shim 返回后 `PVRSRVFreeUserModeMem(local_e70)` 清理处 heap abort，未干净返回，真实 handler/GPU 未验证。
- 证据：`reports/r201-gfx-update-producer.md` + `r201-gfx-update-producer.jsonl`。后续追踪见 r202。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
