# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r124 内核漂移评估；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r124：内核 107→111 漂移评估；只读+离线）

- 运行内核已是 6.12.111（107 headers 并存）；在盘桥 vermagic 实测即
  111，可直接加载，重建不需重编；L1 全绿（226+1 skip，268 C）。
- 会话仍失（无模块、设备解绑、仅 card0）；canvas 脏文件随 test 提交刷新。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r124-kernel-drift.md`。
- 遗留：重建批准（含 r113 首帧执行）；59 提交 push 待明确指令。

---

## 本次会话进展（r123：意外重启后复核；只读）

- 外部重启：树完好（b4e0b5a，干净）、L1 全绿（+1 诚实 skip）、
  UMD 留档可用；会话已失（无模块、设备解绑、对象清零）。
- §12 活页已更新 + STATUS 现状加 stale 标注；重建待明确批准。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r123-post-reboot.md`。
- 遗留：重建批准；加载窗口；push；T3 首帧执行。
