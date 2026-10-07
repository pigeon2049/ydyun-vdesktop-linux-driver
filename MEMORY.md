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

## 本次会话进展（r251：UMD standalone flake 现状，批准执行）

- render 路径 standalone 10/10 崩，GDB 下全过（r167 翻版，桥无罪）；flake 率演进 4/16→11/11→6/6→10/10，疑与 uptime 相关。node probe 首跑偶发 2 failing，重跑两次全绿。UMD 链 soak 不可行。详见 `reports/r251-umd-flake-status.md`。
- refs 不变，dmesg 干净。**Freeze 继续。**无代码改动。

## 本次会话进展（r250：observer 在 tqx_ctx 桥下回归，批准执行）

- `=2` + tqx_ctx 下 ping 全套 23 ok + PASS，CCB 行一致；observer × 5 配置正交矩阵补完。拆桥干净，默认恢复。详见 `reports/r250-tqxctx-observe-regression.md`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
