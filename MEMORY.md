# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](memory/MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](memory/MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](memory/MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](memory/MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](memory/MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](memory/MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](memory/MEMORY-HISTORY-2026-10-08.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r343：修正 r342，离线）

- 反汇编闭合：rdx 缓冲 `[0,0x820)` 由 `rep stos` 清零（`0x3ffc→0x400b`，`%r12` 自 `0x3c5c` 未改写），`+0x820` 起的非零值为残留栈，撤回 r342“app 填入”解读。计数槽仍空，生产者仍待 transfer 侧 RE（`RGXTDMQueueTransferNew` 0x614e0）。零硬件触碰，freeze 继续。
- 遗留：`RGXTDMQueueTransferNew` 参数消费（离线）。
---

## 本轮进展（r342：app 入参活体，批准执行）

- GDB 活体单发（`=2` 窗口）：断 app `0x4026`，rdx 缓冲 `+0x820/+0x828/+0x838` 非零（栈指针），计数槽仍零；收回 r341“清零后无回填”的说法。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/app-args-window.sh`。
- 遗留：app `0x3f00–0x4030` 离线反汇编，命名 rdx 缓冲写入来源；计数槽（ctx 链）与 rdx 缓冲的对应待确认。
---

