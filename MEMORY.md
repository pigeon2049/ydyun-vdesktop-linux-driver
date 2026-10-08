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

## 本轮进展（r342：app 入参活体，批准执行）

- GDB 活体单发（`=2` 窗口）：断 app `0x4026`，rdx 缓冲 `+0x820/+0x828/+0x838` 非零（栈指针），计数槽仍零；收回 r341“清零后无回填”的说法。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/app-args-window.sh`。
- 遗留：app `0x3f00–0x4030` 离线反汇编，命名 rdx 缓冲写入来源；计数槽（ctx 链）与 rdx 缓冲的对应待确认。
---

## 本轮进展（r341：尾跳调用方，批准执行）

- GDB 活体单发（`=2` 窗口）：返回地址归属 app `0x402b`，`QueueTransferNew+0x46` 尾跳进 JobSubmit——调用方点名，`bt` 静默根因亦明。生产者即 copy-setup 自身（`rep stos` 后无回填）。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/retaddr-window.sh`。
- 遗留：app `0x3f00–0x4030` 离线反汇编（优先）或断 `0x4026` 活体读参，另行开轮。
---
