# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r129 fill 契约分歧；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r129：fill smoke 红，契约分歧；批准执行）

- smoke 倒在 bad[3]（0-syncobj 期望 EINVAL，驱动接受执行）；
  旧测试 vs 现驱动分歧，非回归；绘制像素待契约裁决后重跑。
- 旁证 +1 fill 已执行（1/1/0）；模块已卸（同签名 WARN 累计 6）；
  probe 引用 61→87（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r129-fill-contract-dispute.md`。
- 遗留：契约离线裁决；绘制像素；66 提交未 push。

---

## 本次会话进展（r128：RT 绑定帧成功；批准执行）

- 单帧 RT 绑定 DM2（frame_tag=2）：seq=2，completed+1，faulted=0；
  64KiB 读回全 0x5a（空 marker 无绘制，符合设计）；模块已卸。
- probe 引用 35→61（+26，同 r44；r127 的 +34 差异未解释）；
  桥探针全绿，会话健康，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r128-rt-bound-frame.md`。
- 遗留：真绘制内容仍无；65 提交未 push。
