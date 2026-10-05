# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r166 活体会話重建+freeze；L4 候选）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r166：活体会話重建，批准执行）

- 真机调试已批准。`cold_disconnect` 0/1（idle/`guest=0 firmware=1`，双 clean rmmod）→ `fresh-trial --run --runtime-context` rc=0（trial `20261005T161706Z-cf0d876e`，fw sha `35d40f75…`，`guest=2 firmware=2` pinned ref=1）→ 桥默认加载（`card1`/`renderD128`）→ L3 全绿（node 0 failing/0 mismatch；dma smoke PASS，refs 平衡）。dmesg 无新增 WARN/BUG/Oops，r150 Oops 未复现但根因未命名。
- **Freeze**：probe ref 1、bridge ref 0，不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd` 继续禁用。证据：`reports/r166-session-rebuild.md`。候选下一步：L4 阶梯或 update 活体验证（需批准）。

---

## 本次会话进展（r165：复核）

- 零硬件触碰，无代码改动。门禁重跑 269+272 全绿；blit 重放复现 r158/r160（同 VA/PMR，39B/27 runs）；SHA、无模块、r157–r164 文件逐项存在。订正 STATUS 现状两处过期；指针 trailing 属 bA32 惯例不动。
- 证据：`reports/r165-verification.md`。遗留：57 项历史包袱未动。

---

