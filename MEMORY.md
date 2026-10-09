EXIT:0
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
> r422 轮按 §4 清理：r420 节已移入归档。
> r408 轮按 §4 清理：r406 节已移入归档。
> r409 轮按 §4 清理：r407 节已移入归档。
> r410 轮按 §4 清理：r408 节已移入归档。
> r412 轮按 §4 清理：r410 节已移入归档。
> r414 轮按 §4 清理：r411 节已移入归档。
> r415 轮按 §4 清理：r412 节已移入归档。
> r417 轮按 §4 清理：r414 节已移入归档。
> r418 轮按 §4 清理：r415 节已移入归档。
> r419 轮按 §4 清理：r419 节补入（前轮遗漏）。
> r420 轮按 §4 清理：r418、r417 节已移入归档。
> r421 轮按 §4 清理：r419、r418、r417 节已移入归档。
> r425 轮按 §4 清理：r421 节已移入归档。

> r427 轮按 §4 清理：r425 节已移入归档。

## r427 (2026-10-09): Render Target 元数据结构逆向——psKickTA 全布局已测，结构本体为固件私有（离线反汇编）

- psKickTA 完整 18-qword 布局 [MEASURED]（FUN_0017d890，decompiled.c:54347）：[0]=RTData entry+0x08、[1]=TA_buf+0x10=RTData entry+0x00（render target VA）、[2]=*(ctx+0x3d8+idx*8)、[3]=TA_buf+0x28=*(ctx+0x440)、[4]=magic 0x3089705f3089705f（UMD 写入）、[5]=*(ctx+0x10)、[10]=TA_buf+0x30=*(ctx+0x448)、[12]=0x10 常量。
- RTData 条目 0xD0 字节字段表 [MEASURED]：+0x00→psKickTA[1]、+0x08→psKickTA[0]、+0x48/+0x50 sync、+0xC8 缓存、+0x118/+0x120 sync；数组基址=lVar3+0x38，条目=lVar3+0x38+idx*0xD0。
- Render context state 关键偏移 [MEASURED]：+0x24 buffer 索引、+0x440→TA_buf+0x28、+0x448→TA_buf+0x30。
- RGXAddRenderTarget 创建流程：RGX_RT_ALLOCS、parameter memory、MLIST、VHEAP；RTDataSet 分配者在 UMD 之外（未找到）。
- 结论：psKickTA[1] 指向的结构本体为固件私有——UMD 只透传 VA，布局无法从 UMD 反汇编确定；方案 B 已达边界。r428 前置：选项 A（捕获真实 UMD Header）或选项 C（3D 路径验证 T2）；P0 完成前不得活体。
- 纯离线零硬件；门禁 550+1416 全绿；无代码变更。

## r426 (2026-10-09): +0x10 指向 render-target 元数据结构——真实 Header 需 UMD 上下文状态（离线反汇编）

- FUN_00178800（RGXPrepareTA）完整写入清单 [MEASURED]：+0x10=*(render_ctx+idx*0xD0+0x38)（per-buffer 描述符数组，非原始像素 BO）；+0x28=*(render_ctx+0x440)、+0x30=*(render_ctx+0x448)；+0x68 布尔标志；+0x120 位打包；+0x50/0x58 经 FUN_00184220。
- +0x10 语义：render-target 元数据结构的设备地址，由 UMD 在 render context 创建时分配并初始化到描述符数组+0x38 处。PowerVR render target 是固件可解析的结构（含颜色/深度缓冲地址、tile 配置等），非裸像素缓冲。
- FUN_0017d890（psKickTA 构建）[MEASURED]：[1]=*(TA_buf+0x10)、[4]=magic 0x3089705f3089705f（UMD 写入）、[3]=*(TA_buf+0x28)、[10]=*(TA_buf+0x30)。
- r425 超时根因：16KB 像素 BO 非有效元数据结构，固件按结构布局解析垃圾→挂起。r414 全零=psKickTA[1]==0→"无工作"快路径。
- 真实 Header 大多数字段指向 UMD 上下文内部状态，无法从零构造。r427 前置：捕获真实 UMD TA Header 回放（推荐）或逆向 render context 初始化；P0 完成前不得活体。
- 纯离线零硬件；门禁待跑（无代码变更）。

