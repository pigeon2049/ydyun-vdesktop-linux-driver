# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本次会话进展（r257：RGXKickGfx 签名恢复 + /tmp 调查，零硬件触碰）

- 开工声明零硬件触碰。用户要求重建 r203 harness 命令：GDB 脚本全裸 `run`、history 无记录，从反汇编恢复 6 参数用途（rdi=render ctx，rsi=kickA，rdx=b24，rcx=kickB，r8=b25，r9=栈参）+ 调用链；b20/b22 字段偏移待 GDB 确认。详见 `reports/r257-kickgfx-signature.md`。
- /tmp 用途查清：shim 默认 append 到 `/tmp/opencode/umda/trace.jsonl`（量大防 git 污染 + r67 灌满教训）；易失放 /tmp，精选入库（r212 起）。
- 未改码、未跑门禁、未碰会话。
- 遗留：GDB 确认字段偏移 + fabricated 重建 + 真桥重放。USB 短页标题日期问题留待对应轮。
---
