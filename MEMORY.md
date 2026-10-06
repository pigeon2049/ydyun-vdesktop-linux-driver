# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r193 psKickTA 构造；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r193：psKickTA 构造）

- 零硬件触碰。psKickTA 无铸造函数；锚点是真实 render 上下文
 （`+0xc`）；`+0x1c8` 由 PrepareTA 回填；features `+0x54` 门内
 第二次出现。下步手塑回合可在 fabricated shim 下离线做，
 无需硬件批准。
- 证据：`reports/r193-taskickta-shape.md`。候选下一步：手塑回合（离线）/ 活体 ladder（待批）。

---

## 本次会话进展（r192：TA producer 收敛）

- 零硬件触碰。`nm -D` 实测 `RGXKickTA` 等为导出符号——producer
 不需罐装 3D 程序，harness 直调 `RGXKickTA` 可达 TA 链；
 前置为 `psKickTA+0x30`（PrepareTA 产物，r85 的墙）。
 调用边确认 update 编组只活在提交链内。
- 证据：`reports/r192-ta-producer.md`。候选下一步：psKickTA 构造 recon（离线）→ 活体 ladder（待批）。

---

