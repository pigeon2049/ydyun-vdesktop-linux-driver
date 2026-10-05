# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r165 复核全绿；两处过期已订正）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r165：复核）

- 零硬件触碰，无代码改动。门禁重跑 269+272 全绿；blit 重放复现 r158/r160（同 VA/PMR，39B/27 runs）；SHA、无模块、r157–r164 文件逐项存在。订正 STATUS 现状两处过期；指针 trailing 属 bA32 惯例不动。
- 证据：`reports/r165-verification.md`。遗留：57 项历史包袱未动。

---

## 本次会话进展（r164：快照刷新 pass）

- 纯文档，零硬件触碰。快照 §5→r157–r163 状态、§6 计数→269 Python（1 skip）+272 C 并补 11 个门禁文件行、STATUS L1→269、快照时间→2026-10-05；`对应提交` 先提交后 amend（bA32 做法）。§7 长期项与历史章节未动。
- 门禁实测复核全绿；translator 方法数订正为 6（r159 文“4 项”为口径差）。证据：`reports/r164-snapshot-refresh.md`。

---

