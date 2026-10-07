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

## 本次会话进展（r245：L4 legacy 部分，批准执行）

- rung7 compute 全过 exit 0；rung5/6/8 在 render create 处 standalone 6 连崩，GDB 下全过（r167 翻版，桥无罪）。refs 不变，L3 全绿。详见 `reports/r245-l4legacy-partial.md`。
- **Freeze 继续。**无代码改动。

## 本次会话进展（r244：legacy 真实 blit，批准执行）

- 默认桥上真实 blit 止于 `0x89:0x0` → -25，UMD 中止未到 submit；trace 8205 行已入库；refs 不变，L3 全绿。详见 `reports/r244-legacyblit-refused.md` + `.jsonl`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
