# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r103 格式全灭；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r103：格式表解出但全灭；离线自主）

- 格式能力表（bit7/bit8 谓词，fmt0 非法）；13 值实测全返 3；
  下步：扫宽/高 或 读 validator 后分支。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r103-format-table.md`。
- 遗留：维度/分支；加载窗口；push 待批。

## 本次会话进展（r102：Layout 门是值驱动；离线自主）

- 栈位 a6–a15 全扫放 buffer，Layout 全返 3：门不在指针在值；
  下步 validator 枚举映射（0x16010/0x15ce0 + 查表逻辑）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r102-layout-valuedriven.md`。
- 遗留：枚举映射；加载窗口；push 待批。
