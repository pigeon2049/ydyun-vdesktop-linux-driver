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

## 本次会话进展（r264：重启后重建，批准执行）

- r263 死锁重启后按 r211 流程重建：cold 0/1 双 clean；新 trial `20261007T131341Z-3b9ae877`（Guest/FW 2/2 pinned，ref 1）；默认桥（`card1`/`renderD128`，ref 0）；L3 全绿；dmesg 干净。在载桥是 r263 含死锁构建，默认参数下 slices 路径休眠。详见 `reports/r264-session-rebuild.md`。
- **Freeze 即刻生效。**
- 遗留：锁序修复（离线）；slices 重验；fire 函数。USB 短页标题日期问题留待对应轮。
---
