# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r97 P 槽观测；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r97：P 槽位直接观测；离线自主）

- 断 CCB 出口：P 有效（堆），`+0x50=0x3000/+0x54=0/+0x58=0xf`；
  问题收敛为"+0x54 写入者是谁"（缺省零 vs 未调用的前置步骤）。
- 下步：硬件观察点抓写入者。零硬件触碰。
  证据：`mt-vgpu-guest/reports/r97-p-slot-observed.md`。
- 遗留：+0x54 写入者；加载窗口；push 待批。

## 本次会话进展（r96：TDM size-0 根因；离线自主）

- DebugPrintf 全参链：size = `[P+0x54]<<7`，P 有效计数 0；
  General 堆无辜；harness 参数与此无关（头部只用 rdi）。
- 下步：跟 R2DCreateContext 内 byte+8 装配源。
  教训：无符号内部断点走 catch-load 换算或 DebugPrintf 模板。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r96-tdm-count-zero.md`。
- 遗留：计数槽来源；加载窗口；push 待批。
