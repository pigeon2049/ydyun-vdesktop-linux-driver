# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r118 push 审计；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r118：push 队列审计；离线自主）

- 52 提交审计干净可推（代码/文档分离、无产物、r72–r117 不断号）；
  push 本体未执行，等批准。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r118-push-audit.md`。
- 遗留：push；加载窗口；T3 首帧执行。

## 本次会话进展（r117：日终活体盘点；离线自主）

- 全量只读复核零漂移（23/34/38/1/0，dmesg 0 WARN，D 态 0）；
  D 态误报教训（comm 首字母 D，须精确匹配 STAT 列）。
- 待办：50 提交待 push；加载窗口/特性开关/新会话执行待批。
- 证据：`mt-vgpu-guest/reports/r117-dayend-attestation.md`。
- 遗留：push；加载窗口；T3 首帧执行。
