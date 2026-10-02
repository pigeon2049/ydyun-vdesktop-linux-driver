# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r100 sutu 可用；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r100：sutu 设备选择可用；离线自主）

- `sutu_dev_select(128)` 返回 0（真枚举选中我方桥）；DevInit 不需要；
  +0x54 指向 surface 创建链（Layout 头部已开头）。
- 方法论：先读 NULL 门再调；fabricated 枚举走真实文件系统。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r100-sutu-select.md`。
- 遗留：Layout/Surface 整形；加载窗口；push 待批。

## 本次会话进展（r99：+0x54 从未被写入；离线自主）

- 三种布局一致：该槽无人写（r98 calloc 复用是布局噪声）；
  r98"悬空"修正为"未初始化"；缺的是前置调用序列。
- 下步：延伸序列（DevInit→select→Create→Layout→Surface）逐加查槽。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r99-slot-never-written.md`。
- 遗留：序列延伸；加载窗口；push 待批。
