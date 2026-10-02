# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r93 到 142 条；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r93：Rogue2D 95→142；离线自主）

- 两次零句柄修复（0x6:0x3 import，0x89:0x0 context）：95→141→142；
  到达 TransferContext 创建；r92 空连接嫌疑同步证伪（rdi 有效）。
  代码提交 `7ad8663`（27 行，-Werror 干净）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r93-rogue2d-142calls.md` + jsonl。
- 遗留：create 后 16 条窗口；加载窗口；push 待批。

## 本次会话进展（r92：判别式 verdict；离线自主）

- OUT 逐字节一致 → 输入侧问题；嫌疑 `AcquireCPUMapping` 空连接
 （`lVar2+0x10` 无人写）；证伪命令已备好（gdb 读槽 / 读完 8aaa0）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r92-discriminant-verdict.md`。
- 遗留：+0x10 证伪；加载窗口；push 待批。
