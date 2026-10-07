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

## 本轮进展（r282：fire 独立模块离线实现，零硬件触碰）

- r281 绕行：新模块 `mt_live_tqx_fire` 自有 render 节点（`mtlivefire`，零 ioctl）+ 自有 64 页 space/TQX ctx/slices/scratch，直连 probe 会话，不碰 bridge/renderD128。分块 fire（21 块/21 fence/验尾块），root-only 单发，失败行号上报，锁序沿 r265。
- 门禁 9 新项（含反向；修门禁自身 `cls.run` 覆盖 bug）。`check-offline` 363+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：独立模块活体（需批准：insmod 不碰 bridge → 新节点 + fuser 空 → run → dmesg → rmmod）。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r281：分块 fire 活体被持有挡回，批准执行，未触硬件）

- 三开重载第一步即被拒：`renderD128` 被 PID 60651（会话桌面自身）持有，bridge ref 1，`rmmod` 报 `in use` 即停手（未 `-f`、未 insmod、无提交）。refs 仍 1/1，本轮窗口零新增 WARN。空暂存区 `build/traces/r281/`。
- 遗留：holder 释放（需用户侧，agent 关不掉自家桌面）后重跑三开验证（`chunks=21` + `verified=1`）；r280 代码在盘未上机。2 提交仍在本地未 push（用户选择暂不推送）。
---

