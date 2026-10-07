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

## 本次会话进展（r276：WARNING 修复验证，批准执行）

- WARNING 真条件定位（prepare 失败路径 teardown）+ 修复前移后重验：三开 blit 必失败路径后零 WARNING；fail_at 行号+6 机制自证。门禁 +1；`check-offline` 351 OK；`make kernel` 零警告。拆桥干净，默认 + L3 全绿。详见 `reports/r276-warning-fixed.md`。
- **Freeze 已恢复。**
- 遗留：bind 内部分项定位。USB 短页标题日期问题留待对应轮。
---
