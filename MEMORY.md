# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r112 收官判断；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r112：legacy-TDM 疑死代码；离线自主）

- ladder 预热后链不变：五堆条件同果 → 结构性零；
  真 2D 只走新 DDK 分支；停 fabricated 整形（27 轮收官）。
- 出路：加载窗口（含 features 开关评估）；离线只剩 T3 收尾 + 首帧设计。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r112-legacy-tdm-dead.md`。
- 遗留：加载窗口（关键）；T3 收尾；push 待批。

## 本次会话进展（r111：chunk 解剖；离线自主）

- P 块 0x40（usable 56），读 next-chunk 数据 `{0x3000,0,0xf}`；
  spray 失败因代际差 130KB；备选捷径：可控内容分配。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r111-chunk-anatomy.md`。
- 遗留：邻居类型/可控分配；加载窗口；push 待批。
