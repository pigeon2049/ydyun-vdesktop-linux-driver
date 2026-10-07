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

## 本次会话进展（r277：bind 黑盒未打开，批准执行）

- bind_boot_shared 内部细分未果：bind_many/bind_pools/build_pages/borrow/plan 逐处打印后活体全无新行；fail_at 仍报调用行；新打印行系统性缺失未解。拆桥干净，默认 + L3 全绿；暂存区未建（笔误，下轮注意）。详见 `reports/r277-bind-blackbox.md`。
- **Freeze 已恢复。**诊断打印是否保留待定。
- 遗留：换手段定位或 scratch 分块绕行。USB 短页标题日期问题留待对应轮。
---
