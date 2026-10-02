# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r87 Rogue2D spike；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r87：Rogue2D fabricated spike；离线自主）

- 单个 `R2DCreateContext` 走 95 条桥调用后干净 unwind（返 3）；
  卡点 `0x89:0x5 GetSharedMemory`（OUT 20：eError + 2 指针，fabricated 零填充）；
  我方桥 0x89 组全空（`-ENOTTY`）。0x89 TDM 全表 11 项已列；
  最小实现评估：复用 pmr_new + mmap，比 DDK2 门小。
- 零硬件触碰（fabricated 结论自足，未跑活体）。
  证据：`mt-vgpu-guest/reports/r87-rogue2d-spike.md` + jsonl。
- 遗留：0x89:0x5/0x6 实现立项（含重载）；T3 继续；push 待批。


## 本次会话进展（r86：真绘制栈 recon；离线）

- 用户令快速推进：GLES 无 EGL（0 导出），排除；Rogue2D 入选——
  96 导出、仅依赖 libsrv_um（同桥）、建 ctx→建面→填充→等 fence 四步、
  自带测试脚手架 + sutu 初始化。spike 序列已给（fabricated 先行）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r86-rogue2d-first-draw.md`。
- 遗留：Rogue2D spike 已执行（见上节）；特性开关 + push 待批。
