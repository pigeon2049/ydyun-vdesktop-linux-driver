# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r125 新会话重建完成；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r125：新会话重建完成；批准执行）

- cold-disconnect（finish=0/1）→ fresh-trial（2/2 retained）
  → 桥加载（build-id `2c6bede3…`）→ L3 全绿 → L4 八级全 0。
- 终态：probe 引用 1 / bridge 引用 0，dmesg 干净，无 D 任务；
  会话即刻起 freeze。证据：`mt-vgpu-guest/reports/r125-session-rebuild.md`。
- 遗留：r113 首帧执行下一轮单独确认；61 提交未 push（用户明确暂不 push）。

---

## 本次会话进展（r124：内核 107→111 漂移评估；只读+离线）

- 运行内核已是 6.12.111（107 headers 并存）；在盘桥 vermagic 实测即
  111，可直接加载，重建不需重编；L1 全绿（226+1 skip，268 C）。
- 会话仍失（无模块、设备解绑、仅 card0）；canvas 脏文件随 test 提交刷新。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r124-kernel-drift.md`。
- 遗留：重建批准（含 r113 首帧执行）；59 提交 push 待明确指令。
