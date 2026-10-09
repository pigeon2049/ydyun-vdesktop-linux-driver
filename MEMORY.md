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
> r428 轮按 §4 清理：r426 节已移入归档。
> r429 轮按 §4 清理：r427 节已移入归档。
> r430 轮按 §4 清理：r428 节已移入归档。
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

## r430 (2026-10-09): TA Header +0x10 = RgnHeader device VA——3-hop 链实证；纠正 r428 文件归属（离线反汇编）

- **核心结论**：TA Header `+0x10` = **RgnHeader device VA**（[MEASURED] 三跳链）：
  1. `RGXAddRenderTarget:49312`：`local_5b0[1] = local_6d8`（RgnHeader dev VA → VA 表）
  2. `SetupRTDataSet:48867`：`*(RTDataSet+0x38) = *(param_4+8)` = local_5b0[1] → RTData entry+0x00
  3. `RGXPrepareTA:52144`：`*(TA_buf+0x10) = RTData entry+0x00`
  → `psKickTA[1]` = RgnHeader VA；固件解析 RgnHeader 获 tile 布局。r425 超时（+0x10=原始像素 BO）彻底解释。
- **RgnHeader** [MEASURED]：size = `numRT × round_up(tiles×0x40,64)`（64×64 → 0x100B）；
  UMD 经 `InitRegionHeaderBuffer` 预填全 `0xFFFFFFFF`（heap=0x133 路径）；`DevmemAllocateAndMap` 设备可见。
- **MLIST** [MEASURED]：size = `numRT × 0x4a000`（config+0x5c，另有 0x72000 变体）；固件写入，不预填。
- **纠正 r428**：`RGXAddRenderTargetDDK2` = `linux-legacy-umd-5.2.0/decompiled.c:50203`（270 行），
  非 mtdxum64.dll（全语料库 grep：MLIST/RgnHeader 仅 Linux UMD 有；mtdxum64.dll:50202 是 C++ 容器初始化函数）。
- **最小有效 TA Header**：+0x10=RgnHeader VA（分配 0x100B 填 0xFF）；+0x28/+0x30=[UNKNOWN]；
  +0x68=0；其余 0。r431 P0：Linux guest 实现 RgnHeader 分配+初始化。
- 门禁 `check-offline` 全绿；零硬件触碰，纯离线，无代码变更。

## r429 (2026-10-09): Windows 驱动 TA 提交结构提取——D3D11 UMD 用 0x78 字节 kick，非 360B Header（离线反汇编）

- mtdxum64.dll（DX10/11 UMD）构建 0x78 字节 kick 条目（FUN_180224220 @392802、FUN_1802411e0 @412745），magic 0x3089705f3089705f 在 **[1]**（Linux UMD psKickTA[4]），经 D3DDDIEscapeCb 提交；调用者在 FUN_18021cf50（0x78 步长数组，清零 15 qword 后逐条填充）。
- kick 字段表 [MEASURED]：[0]=VA、[2]=0x100000000、[3]=2、[8]=像素格式映射、[9]=维度打包 ((h-1)<<16|(w-1))、[10]=VA&~0xf、[0xb]=VA>>4。
- 0x168 的 151 次命中去噪：~140 vtable 偏移 + ~8 C++ 对象大小均为噪声；**Windows 侧无 360B TA Header 分配**——D3D11 抽象层不同。
- MTT 特有 delta：kick 布局与 Linux 不同，但 render-target 元数据仍为固件私有（r427 结论不受影响）。
- 结论：**360B TA Header 为 Linux UMD 特有**；同一固件接受 D3D11 kick 与 Linux TA Header——提交格式由 UMD/KMD 协商。
- r430 前置：Linux 侧 RGXAddRenderTargetDDK2 的 MLIST/RgnHeader 布局（render-target 元数据最佳线索）。
- 零硬件触碰，纯离线；无代码改动。

