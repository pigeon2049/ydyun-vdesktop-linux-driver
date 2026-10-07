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

最后更新：2026-10-07（r196 fabricated RGXKickGfx producer；r195 commit 5422681 已 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r196：RGXKickGfx update producer）

- 零硬件触碰（fabricated）。`RGXKickGfx` 到达 `SubmissionSetUpdateSyncPrim`，实测 count=1、首项 flag=2；trace 发出 `0x82:0x14`（IN 108/OUT 4），fake shim 返回 0，函数返回 0。达到 producer 前手动清零 render slot（`+0x24`）；正常初始化来源未明，不能据此声称真实 CCB 执行或桥接支持。
- 证据：`reports/r196-gfx-update-producer.md` + trace。下一步追 slot 的合法初始化路径；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r195：flag&2 手塑改走 producer 层）

- 零硬件触碰（fabricated）。连接对象写入 flag=2 sync 条目后 `RGXKickTA → 3`，trace 无 kick ioctl。SHA 对版调用图表明 `RGXKickTA` 不调用 `SubmissionSetUpdateSyncPrim`；RGXKickGfx 等才走该 helper。r194 选错测试入口，下一步转 producer 层。
- 证据：`reports/r195-takick-flag2.md` + `r195-takick-flag2.jsonl`。会话仍 freeze；未跑 live observer。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184 排序，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---
