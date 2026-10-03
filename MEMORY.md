# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r123 重启复核；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r123：意外重启后复核；只读）

- 外部重启：树完好（b4e0b5a，干净）、L1 全绿（+1 诚实 skip）、
  UMD 留档可用；会话已失（无模块、设备解绑、对象清零）。
- §12 活页已更新 + STATUS 现状加 stale 标注；重建待明确批准。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r123-post-reboot.md`。
- 遗留：重建批准；加载窗口；push；T3 首帧执行。

## 本次会话进展（r122：过期表述清扫；离线自主）

- 全入口 grep 唯一命中 STATUS L1 计数，已改 226；其余无过期。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r122-stale-sweep.md`。
- 遗留：加载窗口；push；首帧执行。
