# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](MEMORY-HISTORY-2026-10-08.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r283：独立模块首次真发射全绿，批准执行）

- 自有节点 bring-up 一次过（`prepared=1` + `slices: ready` + `card2`/`renderD129`）；活体三处反馈即修：completed 门删除（终身计数≠就绪）/串行流（slices 外借语义）/verify 补 `upload_dev`。终轮 `fired=1 chunks=21 verified=1 bad=0/1310720 result=0`。
- 拆模块干净（节点消失，refs 回 1/1）；窗口零新增 WARN。门禁 366+292 全绿；`make kernel` 零警告；反向验证通过。
- **Freeze 已恢复（bridge/probe/renderD128 全程未碰）。**
- 遗留：合并策略 recon（fire 合进 bridge vs 保持独立）或更大矩形/soak。本地提交仍未 push。
---

