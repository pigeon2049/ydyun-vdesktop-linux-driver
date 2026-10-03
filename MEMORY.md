# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r121 信封固定；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r121：r113 修正案；离线自主）

- DM2 固定 0x46f0 信封，模板即最小合法形状；空=模板+tag、无 RT；
  首帧零新增代码假设成立；唯一活体问题是固件接受性。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r121-envelope-fixed.md`。
- 遗留：加载窗口；push；首帧执行（单点：无 RT 接受性）。

## 本次会话进展（r120：加载窗口 runbook；离线自主）

- worktree 重建 da3df8b 编出第三 build-id → bit 回滚已证伪；
  runbook 按功能级回滚编写（7 步 + 中止条件）；未执行加载。
- 零硬件触碰（worktree 已删）。证据：`mt-vgpu-guest/reports/r120-load-window-runbook.md`。
- 遗留：加载窗口执行；push；T3 首帧执行。
