# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r95 零尺寸分配；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r95：TransferContext 卡零尺寸分配；离线自主）

- B 路一试：DebugPrintf 四连定位 `DevmemAllocateAndMap:1`
  （size 0；General 堆已 resolved）；下步 gdb 读 SubAllocate 入参
  定尺寸槽来源（疑 `0x1:0xc` fabricated 零回包）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r95-zero-size-alloc.md`。
- 遗留：尺寸槽；加载窗口；push 待批。

## 本次会话进展（r94：0x89:0x0 线上成功但自检 unwind；离线自主）

- 0x89:0x0 IN 解码正常、桥成功，UMD 紧接拆除：CCB 约 10 道门查
  rogue2d 内建状态（`*(+8)` 等），与桥 OUT 无关。
- 路径判断：B（sutu 正路）优先，A（自底整形）备用。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r94-tdm-create-selfcheck.md`。
- 遗留：B 路一试；加载窗口；push 待批。
