# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r110 喷洒 verdict；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r110：喷洒 verdict；离线自主）

- 大块喷洒无效（尺寸类隔离）；perturb 下 size-0 依然（新鲜零）；
  意外：整流重试环（284=142×2，0x19 触发）。
- 死锁完整：context 要 surface 填数，surface 要 context 给堆；
  下步找第三调用（dev-select-ex？），停试参。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r110-spray-verdict.md`。
- 遗留：第三调用；加载窗口；push 待批。

## 本次会话进展（r109：P+0x54 是堆越界读；离线自主）

- `malloc_usable_size(P)=56 < 0x58`：厂商代码堆越界读邻居；
  我方流程邻居为 0，真应用大概率蒙混过关。
- 打法转向：堆喷洒（廉价）替代精确整形；T1 钳制设计再确认。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r109-oob-read.md`。
- 遗留：堆喷洒验证；加载窗口；push 待批。
