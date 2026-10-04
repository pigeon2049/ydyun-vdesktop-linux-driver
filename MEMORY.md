# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r130 契约裁决+首个绘制像素；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r130：契约裁决，测试过期；批准执行）

- 离线裁决：驱动 `if (r->out_syncobj)` 自 r40，强制要求从未存在；
  测试期望是 day-one 误期。改测试（bad[3]→正向断言）+ uapi 注释。
- 活体 smoke 全绿：13 非法拒、0-syncobj fill、16×16 像素三重验证、
  copy 闭环；3/3/0，模块已卸（WARN 累计 8，同签名）。
- probe 引用 87→113（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r130-fill-contract-verdict.md`。
- 遗留：67 提交未 push。

---

## 本次会话进展（r129：fill smoke 红，契约分歧；批准执行）

- smoke 倒在 bad[3]（0-syncobj 期望 EINVAL，驱动接受执行）；
  旧测试 vs 现驱动分歧，非回归；绘制像素待契约裁决后重跑。
- 旁证 +1 fill 已执行（1/1/0）；模块已卸（同签名 WARN 累计 6）；
  probe 引用 61→87（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r129-fill-contract-dispute.md`。
- 遗留：契约离线裁决；绘制像素；66 提交未 push。
