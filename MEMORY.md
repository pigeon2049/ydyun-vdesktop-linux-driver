# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r96 计数槽空；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r96：TDM size-0 根因；离线自主）

- DebugPrintf 全参链：size = `[P+0x54]<<7`，P 有效计数 0；
  General 堆无辜；harness 参数与此无关（头部只用 rdi）。
- 下步：跟 R2DCreateContext 内 byte+8 装配源。
  教训：无符号内部断点走 catch-load 换算或 DebugPrintf 模板。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r96-tdm-count-zero.md`。
- 遗留：计数槽来源；加载窗口；push 待批。

## 本次会话进展（r95：TransferContext 卡零尺寸分配；离线自主）

- B 路一试：DebugPrintf 四连定位 `DevmemAllocateAndMap:1`
  （size 0；General 堆已 resolved）；下步 gdb 读 SubAllocate 入参
  定尺寸槽来源（疑 `0x1:0xc` fabricated 零回包）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r95-zero-size-alloc.md`。
- 遗留：尺寸槽；加载窗口；push 待批。
