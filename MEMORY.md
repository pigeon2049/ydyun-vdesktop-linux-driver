# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r108 surface 证伪；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r108：surface 先行证伪；离线自主）

- 跳过 Context 直接调 Surface：返 3 且零桥调用——无条件依赖；
  只剩 +0x54 写入者一个问题（域：Context 内 CCB 调用点之前）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r108-surface-first-dead.md`。
- 遗留：+0x54 写入者；加载窗口；push 待批。

## 本次会话进展（r107：CCB 直调链钉死；离线自主）

- bt 证明 R2DCreateContext 直调 CCB；P=[OUT+0x10] 由 init 子调用填；
  堆跨 run 非确定（方法论三定律）；出路：加载窗口或 surface 先行。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r107-direct-chain.md`。
- 遗留：出路二选一；加载窗口；push 待批。
