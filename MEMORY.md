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

## 本次会话进展（r266：slices 重验通过，批准执行）

- r265 锁序修复生效：新构建上机 + 真实 blit，`tqx slices: ready cores=1` 全现，无死锁无 D 态；blit hanging 60s 被杀系 UMD 行为（可 rmmod，与 r263 泾渭分明）。拆桥干净（probe 30→1 对称），默认 + L3 全绿。trace 已入库。详见 `reports/r266-slices-verified.md` + `.jsonl`。
- **Freeze 已恢复。**无代码改动（r265 代码已在盘）。
- 遗留：fire 函数（离线）；fired/verified 活体。USB 短页标题日期问题留待对应轮。
---
