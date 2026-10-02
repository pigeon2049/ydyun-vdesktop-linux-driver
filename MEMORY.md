# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r111 chunk 解剖；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r111：chunk 解剖；离线自主）

- P 块 0x40（usable 56），读 next-chunk 数据 `{0x3000,0,0xf}`；
  spray 失败因代际差 130KB；备选捷径：可控内容分配。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r111-chunk-anatomy.md`。
- 遗留：邻居类型/可控分配；加载窗口；push 待批。

## 本次会话进展（r110：喷洒 verdict；离线自主）

- 大块喷洒无效（尺寸类隔离）；perturb 下 size-0 依然（新鲜零）；
  意外：整流重试环（284=142×2，0x19 触发）。
- 死锁完整：context 要 surface 填数，surface 要 context 给堆；
  下步找第三调用（dev-select-ex？），停试参。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r110-spray-verdict.md`。
- 遗留：第三调用；加载窗口；push 待批。
