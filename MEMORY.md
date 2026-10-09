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
> r363 轮按 §4 清理：r361 节已移入归档。
> r364 轮按 §4 清理：r362 节已移入归档。
> r365 轮按 §4 清理：r363 节已移入归档。
> r368 轮按 §4 清理：r364 节已移入归档。
> r369 轮按 §4 清理：r365 节已移入归档。
> r370 轮按 §4 清理：r366 节已移入归档。
> r372 轮按 §4 清理：r367 节已移入归档。
> r373 轮按 §4 清理：r368 节已移入归档。
> r378 轮按 §4 清理：r376 节已移入归档。
> r374 轮按 §4 清理：r369 节已移入归档。
> r379 轮按 §4 清理：r377 节已移入归档。
> r380 轮按 §4 清理：r378 节已移入归档。
> r382 轮按 §4 清理：r379 节已移入归档。
> r383 轮按 §4 清理：r380 节已移入归档。
> r384 轮按 §4 清理：r381 节已移入归档。
> r385 轮按 §4 清理：r382 节已移入归档。
> r386 轮按 §4 清理：r385 节已移入归档。
> r387 轮按 §4 清理：r383 节已移入归档。
> r388 轮按 §4 清理：r386、r384 节已移入归档。
> r390 轮按 §4 清理：r387 节已移入归档。
> r391 轮按 §4 清理：r388 节已移入归档。
> r392 轮按 §4 清理：r389 节已移入归档。
> r393 轮按 §4 清理：r390 节已移入归档。
> r394 轮按 §4 清理：r391、r392 节已移入归档。
> r395 轮按 §4 清理：r393 节已移入归档。
> r396 轮按 §4 清理：r394 节已移入归档。
> r398 轮按 §4 清理：r395 节已移入归档。
> r402 轮按 §4 清理：r400、r399 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。
> r404 轮按 \u00a74 清理：r402、r401 节已移入归档。
> r405 轮按 §4 清理：无（仅 r404、r405 两节，保留）。
> r407 轮按 §4 清理：r405 节已移入归档。
> r408 轮按 §4 清理：r406 节已移入归档。


## r408 (2026-10-09): RGXSubmitTA 不构造 TA 缓冲（离线反汇编）

**反汇编**（MUSA DDK 5.2.0 UMD，零硬件）：调用链 RGXKickTA(0x17afd0)→RGXPrepareTA(FUN_00178800,218行)→RGXSubmitTA(FUN_001796b0,709行)→BridgeRGXKickTA3D3(FUN_00137180)→SubmitTADataEnQueue；SubmitTA内0x168=360为立即数尺寸参数、VA取自psKickTA[0]，全函数无缓冲写入/模板填充；PrepareTA只做指针搬运(psKickTA[0..2]=render_ctx+0xb6/b8/ba)+0x1c8-0x1d4状态回填；psKickTA无UMD侧构造函数(r193 corroborate)。

**结论**：360B TA缓冲内容(TA指令流)由客户端3D状态机在调用前生成，RGXSubmitTA只透传VA；r409的最小真实TA包不能从本反汇编直接得到。

**r409方案**(按推荐序)：真实应用trace(用r407 hook捕获真实360B，前置：有3D应用能走到RGXKickTA) > KMD/固件文档找TA命令格式 > 盲探(不推荐，r380教训)。

**诚实边界**：已确认=调用链/VA透传/PrepareTA回填；推断=psKickTA+0x08/+0x10语义、DM包@+0x28为TA VA；未知=360B TA ISA编码、544B缓冲内容、固件真实负载最低条件。

报告 mt-vgpu-guest/reports/r408-tasubmitta-no-construction.md。

## r407 (2026-10-09): TA 命令缓冲捕获机制验证（活体观察）

**活体观察**：冷重启后系统干净（probe trial restored，bridge ref=0）；pre-live T1/T2/T3 全过；桥侧临时 hook（`copy_from_user` + `print_hex_dump`）在 `0x82:0xC` 入口成功捕获 TA 命令缓冲前 64 字节（`ta_size=360` 与 r363 一致）；捕获为 fabricated 测试模式，机制已验证；hook 已移除，源码恢复，桥重载干净构建；dmesg 无 WARN/BUG/Oops。

**TA 包布局（已知）**：DM 包 80B（`MT_FW_COMMAND_BYTES`，opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c，r365 活体验证）；TA 命令缓冲 360B（r363 实测，内容为 TA 指令流，布局待反汇编 `RGXSubmitTA` 解析）；0x82:0xC IN 268B（p_ta_cmd @120，kick_ta @188，ta_cmd_size @264）。

**诚实边界**：捕获的是 fabricated 模式，非真实 UMD 负载（无 3D 应用）；真实 TA 指令流布局未解析；本轮未提交 GPU 工作。

**下一步**：r408 TA 包解析（离线，反汇编分析）。

