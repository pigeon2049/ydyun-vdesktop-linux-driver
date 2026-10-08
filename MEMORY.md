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



## 本轮进展（r353：T2-f，离线 fabricated）

- 端到端验证：在 `0x79c92` 处把 b10 真描述子 poke 进槽 0（`$rdx+0x48`，原 NULL）后，完整 `RGXKickTA` 路径上 `SyncPrimRef` 两次返回 0，`SubmitTA` 未跳错误出口。T2 系列核心问题闭合。零硬件触碰，freeze 继续。
- 新发现：`SyncPrimRef` 成功后下游在偏移 `0x929ce` 处 SIGSEGV（此前被 `→3` 挡住未到达），记为 T2-g 起点。
- 方法：GDB 从头运行 + `set disable-randomization off`（ASLR 开）绕开堆布局崩溃；`$rbx` 在 `0x79c92` 处已被改写，槽位须用 `$rdx+0x48`。另纠正：此前"nohup 触发崩溃"实为 harness 引号 bug（`'b5*+0'` 字面传参致 `b6+16=0`）。
- 遗留：T2-g（`0x929ce` SIGSEGV 定位）。
---

## 本轮进展（r352：T2-e，离线 fabricated）

- b10 描述子直接验证：`SyncPrimRef(b10@0)` 返回 0（成功），`SyncPrimRef(NULL)` 返回 3（基线）；描述子 `+8=1` 符合要求。结合 r351 选中逻辑，回填槽 0 `+0x48` 后应成功。零硬件触碰，freeze 继续。
- `r14+0x18` 链静态定位：r14=RGXKickTA `rbp-0x170` 栈结构体，经 PrepareTA 原样传给 SubmitTA；`+0x18` 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（`r14+0x18` 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---

- b10 描述子直接验证： 返回 0（成功）， 返回 3（基线）；描述子  符合要求。结合 r351 选中逻辑，回填槽 0  后应成功。零硬件触碰，freeze 继续。
-  链静态定位：r14=RGXKickTA  栈结构体，经 PrepareTA 原样传给 SubmitTA； 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（ 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---
