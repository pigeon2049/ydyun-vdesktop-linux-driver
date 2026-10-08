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



## 本轮进展（r354：T2-g，离线 fabricated）

- 定性：`0x929ce` 处 SIGSEGV（`mov (%rax),%edi`，`rax=fault_addr=0x6000`）是 **fabricated artifact**，非真实执行链问题。`0x6000` 经 GDB 实测溯源自 `GetSrvHandle @ 0x3c1c0`（`rdi ? *(uint64_t*)rdi : 0`）的返回值——某结构体首 qword 存的是句柄值而非指针；该值经 `0x79733 → 0x7a30b → 0x36ec0 → 0x37111 → 0x92930 (rdi=0x6000,rsi=0x82,rdx=0xc)`，在 `0x929ce` 被当作指针解引用以取 ioctl fd（`ioctl([rax], 0xc0206440=_IOWR('d',64,32), r15)`）。`0x6000` 非法指针在任何真实执行中同样会崩，而官方驱动真机正常，故真实路径下该字段必为有效指针——fabricated harness 未做完整 PVRSRV 建连/句柄表初始化所致。零硬件触碰，freeze 继续。
- 方法修正：pending 断点落在 `RGXKickTA+17` 而非入口，`pc-0x7afd0` 误算 base 差 `0x11` 致首轮行为回退到 `→3`；改由 `info proc mappings` 的 `r--p` 映射取 base 后复现成功（`SyncPrimRef → 0` ×2，再现 `0x929ce` 崩溃）。
- 遗留：fabricated TA 路径天花板已到（下游是设备 ioctl 建连路径）；下一步转向真实 DDK2 render backend（STATUS.md #1）。
---


## 本轮进展（r353：T2-f，离线 fabricated）

- 端到端验证：在 `0x79c92` 处把 b10 真描述子 poke 进槽 0（`$rdx+0x48`，原 NULL）后，完整 `RGXKickTA` 路径上 `SyncPrimRef` 两次返回 0，`SubmitTA` 未跳错误出口。T2 系列核心问题闭合。零硬件触碰，freeze 继续。
- 新发现：`SyncPrimRef` 成功后下游在偏移 `0x929ce` 处 SIGSEGV（此前被 `→3` 挡住未到达），记为 T2-g 起点。
- 方法：GDB 从头运行 + `set disable-randomization off`（ASLR 开）绕开堆布局崩溃；`$rbx` 在 `0x79c92` 处已被改写，槽位须用 `$rdx+0x48`。另纠正：此前"nohup 触发崩溃"实为 harness 引号 bug（`'b5*+0'` 字面传参致 `b6+16=0`）。
- 遗留：T2-g（`0x929ce` SIGSEGV 定位）。
---

