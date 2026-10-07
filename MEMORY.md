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

## 本次会话进展（r263：slices 死锁待重启，批准执行）

- 调用点打印证实 slices 可达，但第二轮卡死在 `mutex_lock(buffers->lock)`（D 态 blit，进程栈 + sysrq 双实锤）；trial_lock→buffers.lock 与持锁路径 AB-BA 死锁（r67 重演）。blit D 态 + 桥 ref 1 + probe ref 44，rmmod 被拒，待重启。详见 `reports/r263-slices-deadlock.md`。
- 教训升级：新锁引入 translator 路径须先锁序审计。
- 遗留：重启重建；锁序修复；fire 函数暂缓。USB 短页标题日期问题留待对应轮。
---
