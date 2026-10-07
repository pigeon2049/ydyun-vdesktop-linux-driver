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

## 本次会话进展（r258：硬盘暂存区流程验证，批准执行）

- 用户纠正后 AGENTS.md §5 改判：大体积易失产物 MUST 写硬盘 `build/traces/`，NEVER 写 `/tmp`。本轮落实：`/tmp/opencode/umda/` 4.2M 清零（已入库删副本，未入库确认无证据后删除）；新流程验证（默认桥 ping 全 PASS + fabricated rung8 全过，trace 落硬盘 25KB）；暂存区已清空。详见 `reports/r258-disk-traces.md`。
- refs 不变，dmesg 干净。**Freeze 继续。**无代码改动（AGENTS.md 约束变更除外）。
- 遗留：GDB 确认字段偏移 + fabricated 重建 + 真桥重放；TQX 真发射；CCB 解读；backend。USB 短页标题日期问题留待对应轮。
---
