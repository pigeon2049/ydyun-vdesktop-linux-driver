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


## 本轮进展（r352：T2-e，离线 fabricated）

- b10 描述子直接验证：`SyncPrimRef(b10@0)` 返回 0（成功），`SyncPrimRef(NULL)` 返回 3（基线）；描述子 `+8=1` 符合要求。结合 r351 选中逻辑，回填槽 0 `+0x48` 后应成功。零硬件触碰，freeze 继续。
- `r14+0x18` 链静态定位：r14=RGXKickTA `rbp-0x170` 栈结构体，经 PrepareTA 原样传给 SubmitTA；`+0x18` 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（`r14+0x18` 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---

- b10 描述子直接验证： 返回 0（成功）， 返回 3（基线）；描述子  符合要求。结合 r351 选中逻辑，回填槽 0  后应成功。零硬件触碰，freeze 继续。
-  链静态定位：r14=RGXKickTA  栈结构体，经 PrepareTA 原样传给 SubmitTA； 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（ 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---

## 本轮进展（r351：T2-d，离线 fabricated）

- 描述子选中步骤定位：`0x79c92: mov 0x48(%rdx),%rdi`，`rdx = rbx+208*i`，`rbx=*(*(r14+0x18)+0x30)`，`i=*(rbx+0x24)`；fabricated 下 i=0，槽0+0x48 为 NULL → `SyncPrimRef` 报 3。GDB 链式复核 match，断点单次命中。零硬件触碰，freeze 继续。
- 遗留：T2-e——`r14+0x18` 堆对象在 kick 结构体中的来源；b10 描述子填槽0+0x48 后复测。
---


