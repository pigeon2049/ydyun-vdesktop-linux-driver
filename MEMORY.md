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



## 本轮进展（r355：DDK2 render backend 缺口盘点，离线）

- 盘点：桥侧 dispatch 三分法——已真实实现（SRVCORE/SYNC/MM、0x88:0x4 翻译、TQX fire、MUSAKICKGFX5 schema）；accept-and-log 空桩（0x82:0x14、0x89:0xa、0x82:0x12/0x88:0x5、0x89:0x8/0x9）；缺失（0x82:0xC、TA firmware 提交通道、per-file VM/BO、UMD 真实建连）。
- 新发现：r354 证据 `0x92930(rdi=0x6000,rsi=0x82,rdx=0xc)` 表明 UMD TA 路径内实际发出 `0x82:0xC`；对照 5.2 生成头为 MUSAKICKGFX2（TA/3D/PR 提交富结构），桥侧无定义、走 `-ENOTTY`。STATUS 口径修正：0x82:0xC 从"S4 边界"升级为具体可排的下一个桥目标；0x81:0x5 维持边界。
- 需求清单 R1–R7：UMD 真实建连（`PVRSRVConnectionCreateDevice`，0xd0 结构；`GetSrvHandle @ 0x13c1c0`=`rdi?*rdi:0`，SHA 对版；`PVRSRVBridgeCall` 即 `FUN_00192930`，ioctl 号 `0xc0206440` 与桥侧 static_assert 同值）→ 0x82:0xC 实现 → 0x82:0x14 执行 → TA 提交通道 → per-file VM/BO（r208）→ DDK2 context 状态 → sync prim 导入验证。
- 分轮分解 r356+（提案）：r356=0x82:0xC wire 入库+observer 占位；r357=UMD 真实建连 recon；r358=0x82:0xC 活体观察（需批准）；r359=TA 提交通道设计；r360=0x82:0x14 执行翻译设计；r361+=实现验收。
- 遗留：r356（0x82:0xC wire 入库）。零硬件触碰，freeze 继续。
---

## 本轮进展（r354：T2-g，离线 fabricated）

- 定性：`0x929ce` 处 SIGSEGV（`mov (%rax),%edi`，`rax=fault_addr=0x6000`）是 **fabricated artifact**，非真实执行链问题。`0x6000` 经 GDB 实测溯源自 `GetSrvHandle @ 0x3c1c0`（`rdi ? *(uint64_t*)rdi : 0`）的返回值——某结构体首 qword 存的是句柄值而非指针；该值经 `0x79733 → 0x7a30b → 0x36ec0 → 0x37111 → 0x92930 (rdi=0x6000,rsi=0x82,rdx=0xc)`，在 `0x929ce` 被当作指针解引用以取 ioctl fd（`ioctl([rax], 0xc0206440=_IOWR('d',64,32), r15)`）。`0x6000` 非法指针在任何真实执行中同样会崩，而官方驱动真机正常，故真实路径下该字段必为有效指针——fabricated harness 未做完整 PVRSRV 建连/句柄表初始化所致。零硬件触碰，freeze 继续。
- 方法修正：pending 断点落在 `RGXKickTA+17` 而非入口，`pc-0x7afd0` 误算 base 差 `0x11` 致首轮行为回退到 `→3`；改由 `info proc mappings` 的 `r--p` 映射取 base 后复现成功（`SyncPrimRef → 0` ×2，再现 `0x929ce` 崩溃）。
- 遗留：fabricated TA 路径天花板已到（下游是设备 ioctl 建连路径）；下一步转向真实 DDK2 render backend（STATUS.md #1）。
---

