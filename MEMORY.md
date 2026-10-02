# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r90 shim 非零句柄；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r90：shim 补 0x89:0x5；离线自主）

- shim fabricated 给非零句柄（`0xa000+`），Rogue2D 95→100 条，
  进入 MapMem（0x6:0x3→0x6:0x6），卡在 import 路径 UMD 侧校验
  （未调 0x6:0x4，直接 unref 走人）；下步跟三层调用定位拒绝字段。
  代码提交 `e0b1d2c`（22 行，-Werror 干净，行为即反向验证）。
- TA（0x82）与 TDM 是独立绘制路径，可并行，谁先出包谁赢。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r90-shim-tdm-shmem.md` + jsonl。
- 遗留：import 校验定位；加载窗口；push 待批。

## 本次会话进展（r89：TDM 下游验证；离线自主）

- H1 走强：MapMem/MapUSCMem 只用已实现桥（0x6 三件套 + mmap/MapToDevice）；
  SubmitTransfer2（0x89:0x4）骨架已列（IN 0x6c/OUT 4，20 参数逐槽）。
- 加载窗口验收判据已写（fabricated OUT 非零 → passthrough Map 真走 →
  SubmitTransfer 包）；H1 证伪预案（拆双 PMR）就位。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r89-tdm-downstream.md`。
- 遗留：加载窗口；T3 继续；push 待批。
