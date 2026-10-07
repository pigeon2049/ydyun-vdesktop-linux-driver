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

最后更新：2026-10-07（r195 flag&2 未改变 RGXKickTA；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r195：flag&2 手塑改走 producer 层）

- 零硬件触碰（fabricated）。连接对象写入 flag=2 sync 条目后 `RGXKickTA → 3`，trace 无 kick ioctl。SHA 对版调用图表明 `RGXKickTA` 不调用 `SubmissionSetUpdateSyncPrim`；RGXKickGfx 等才走该 helper。r194 选错测试入口，下一步转 producer 层。
- 证据：`reports/r195-takick-flag2.md` + `r195-takick-flag2.jsonl`。会话仍 freeze；未跑 live observer。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184 排序，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r194：psKickTA 手塑首轮）

- 零硬件触碰（fabricated）。harness 手塑 psKickTA 调通
 `RGXKickTA → 3` 干净退出：4 崩溃逐一定位（`+0x2d8/+0xb8/+0x28`）；
 关键纠偏——PrepareTA 偏移是元素制（`+0xb6` 实为字节 `0x2d8`）。
 下步造 `flag&2` 条目看 3 是否翻提交。GDB 翻车 3 则已记。
- 证据：`reports/r194-takick-shaping.md` + `r194-takick-return3.jsonl`。
 候选下一步：手塑回合 2（离线）/ 活体 observer（待批）。

---
