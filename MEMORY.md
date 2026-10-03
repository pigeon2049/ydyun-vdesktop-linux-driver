# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r120 runbook；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r120：加载窗口 runbook；离线自主）

- worktree 重建 da3df8b 编出第三 build-id → bit 回滚已证伪；
  runbook 按功能级回滚编写（7 步 + 中止条件）；未执行加载。
- 零硬件触碰（worktree 已删）。证据：`mt-vgpu-guest/reports/r120-load-window-runbook.md`。
- 遗留：加载窗口执行；push；T3 首帧执行。

## 本次会话进展（r119：三线并行收敛；自主）

- 三 subagent：SubmitTransfer 语义齐（仅 2 项推断级）；
  FromDmaBuf 绕不开（维持序）；EGL 不通实锤
  （musa_dri 单 T 导出桩子 + 缺 libglapi）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r119-triple-close.md`。
- 遗留：加载窗口；push；T3 首帧执行。
