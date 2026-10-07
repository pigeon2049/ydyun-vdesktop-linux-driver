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

最后更新：2026-10-07（r201 fabricated GFX update producer 动态复核）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r201：fabricated GFX update producer 动态复核）

- 零硬件触碰。GDB 对上 kick `+0x28` 目标 `+0x200` 与 render-context allocator；修正 `CreateSyncPrim` 输出槽后，CheckSync/UpdateSync 各一项，update `flag=2`、handle 非空，trace 发出 fabricated `0x82:0x14`（IN108/OUT4）。shim 返回后 `PVRSRVFreeUserModeMem(local_e70)` 清理处 heap abort，未干净返回，真实 handler/GPU 未验证。
- 证据：`reports/r201-gfx-update-producer.md` + `r201-gfx-update-producer.jsonl`。下一步低扰动跟踪 local_e70 分配/释放，查明 abort 后重放。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r200：fabricated render context allocator 实测）

- 零硬件触碰。fabricated 默认 shim 下 connect/device/devmemctx/render 全返 0；返回 context `+0x200` allocator 和 `+0x318` SubmissionHead 均非空，trace 117 行。r201 已将其 allocator 与 GFX kick `+0x28` 目标动态对上。
- 证据：`reports/r200-renderctx-allocator.md` + `reports/r200-renderctx-allocator.jsonl`。后续查清 r201 清理 abort。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
