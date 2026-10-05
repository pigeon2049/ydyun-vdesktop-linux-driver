# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r164 快照刷新 pass；执行态见 r162–r163）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r164：快照刷新 pass）

- 纯文档，零硬件触碰。快照 §5→r157–r163 状态、§6 计数→269 Python（1 skip）+272 C 并补 11 个门禁文件行、STATUS L1→269、快照时间→2026-10-05；`对应提交` 先提交后 amend（bA32 做法）。§7 长期项与历史章节未动。
- 门禁实测复核全绿；translator 方法数订正为 6（r159 文“4 项”为口径差）。证据：`reports/r164-snapshot-refresh.md`。

---

## 本次会话进展（r163：tq-perf 同倒于同一 abort 点）

- 零硬件触碰，无代码改动。`musa_tq_performance_test -n 1`（64×64，major 2 + shared backing）510 行后 SIGABRT，无 Submit3；GDB 栈与 r162 copy-blit 三重一致（aborter PC、`TQJobSubmit+738` 返回地址、trace 位置）——同一阻塞点，非新 producer。按名断点因符号不可见 pending，止损。
- 证据：`reports/r163-tq-same-abort.md` + `r163-tq-abort.jsonl`。门禁复核全绿（269+272）。producer 线暂止；候选步骤 2（待可重建会话）或 copy-setup 缺口单独立项。
