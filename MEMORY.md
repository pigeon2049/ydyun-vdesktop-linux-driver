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

## 本轮进展（r291：UMD 驱动 fire 复现全绿 + r290 订正，批准执行）

- 第二窗口零干预全绿：UMD 矩形二次 `fired=1 chunks=21 verified=1`；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第三样本 nonzero=40（轮值第 9 值 `dd 35`，+1 仍未命名）。
- r290 订正（用户指正）：两次“重启”均为手动重开，无自重启证据；90 秒定律作废，约束为用户容忍度。脚本时限收紧（60/60）+ LC_ALL/unset 修已验证生效（本轮 trace 零污染、fired 一次命中）。
- **Freeze 已恢复。**
- 遗留：TA/3D CCB 或 update 的 UMD 驱动验证（需离线 recon producer）。本地提交仍未 push。
---

