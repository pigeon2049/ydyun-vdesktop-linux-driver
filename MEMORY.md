# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r102 值驱动；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r102：Layout 门是值驱动；离线自主）

- 栈位 a6–a15 全扫放 buffer，Layout 全返 3：门不在指针在值；
  下步 validator 枚举映射（0x16010/0x15ce0 + 查表逻辑）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r102-layout-valuedriven.md`。
- 遗留：枚举映射；加载窗口；push 待批。

## 本次会话进展（r101：Layout 可执行 + harness 陷阱；离线自主）

- 真 bug 级教训：裸 `u64` 作 call 参数截断参数表致段错误，须 `u0x..` 形式。
- Layout 三调用全 clean（返 3）；新门 `CreateTestSurfaceLayout` 空指针；
  下步 b31 内容/a6 字段整形。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r101-layout-gate.md`。
- 遗留：b31 整形；加载窗口；push 待批。
