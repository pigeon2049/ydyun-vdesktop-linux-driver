EXIT:0
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
> r431 轮按 §4 清理：r429 节已移入归档。
> r432 轮按 §4 清理：r430 节已移入归档。
> r433 轮按 §4 清理：r431 节已移入归档。
> r434 轮按 §4 清理：r432 节已移入归档。
> r435 轮按 §4 清理：r433 节已移入归档。
> r436 轮按 §4 清理：r434 节已移入归档。
> r437 轮按 §4 清理：r435 节已移入归档。
> r438 轮按 §4 清理：r436 节已移入归档。
> r439 轮按 §4 清理：r437 节已移入归档。
> r440 轮按 §4 清理：r438 节已移入归档。
> r441 轮按 §4 清理：r439 节已移入归档。
> r443 轮按 §4 清理：r441 节已移入归档。
> r444 轮按 §4 清理：r442 节已移入归档。
> r448 轮按 §4 清理：r446 节已移入归档。
> r450 轮按 §4 清理：r447 节已移入归档。

> r451 轮按 §4 清理：r448 节已移入归档。
> r453 轮按 §4 清理：r449 节已移入归档。

## r453 (2026-10-09): TA 命令流与 Bridge 参数检查——最可能根因是未填充的 RgnHeader（离线）

- **TA 命令流 [MEASURED]**：UMD 的 TA 命令就是 360B header 本体，无追加命令
  (`-(uint)(lVar23 != 0) & 0x168`，RGXSubmitTA)；我方 header-only 与 UMD 一致，TA 命令流不是问题。
- **Bridge 参数**：我方 `mt_ta_submit_real` 仅设最小集（ta_cmd_va/size, kick_flags=0,
  upd/fence count=0）；真实 UMD kick 含大量同步原语与 RT dataset，但系刻意最小化测试，
  非 hang 主因 [INFERRED 中置信]。
- **固件包 opcode [MEASURED]**：我方 0x66 来自 work-queue 命名空间（mt_work_opcode），
  真实 KCCB KICK = `0x2ABC0065` (101|magic)，数据为 `RGXFWIF_KCCB_CMD_KICK_DATA`（含 psContext）；
  我方 0x66 = 102 = MMUCACHE 命令号（无 magic）。"MEASURED" 的包布局 (VA@+0x28/size@+0x30)
  其证据 r414 已被 r425 证伪（"无工作"快路径），从未在真实 TA 下验证。
- **最可能根因 [INFERRED 高置信]**：固件在读 header（零=快路径完成，非零=尝试执行→hang）；
  我们的 header 声明"有 TA 工作"但 RgnHeader (0x7c000000) 只是 dword 1s 初始化值、
  从未填入有效 region 数据（UMD `InitRegionHeaderBuffer` 后会填真实数据）；
  固件解析垃圾 region header 时 hang。
- 下一步 P0：研究 UMD 在初始化后填入 RgnHeader 的真实内容，最小有效 region header 格式。
- 纯离线轮，零硬件触碰，无生产代码变更。
- 报告：mt-vgpu-guest/reports/r453-ta-cmd-bridge-params-rgnheader-root-cause.md

## r452 (2026-10-09): DDK psKickTA flags 深度分析——+0x68 语义与 +0x120 逐位含义（离线）

- **+0x68 = ((flags & 3) == 3)** [MEASURED]：bit0 与 bit1 全置才写 1；DDK 位定义 [UNKNOWN]（专有 DDK）；单 RT 纯 TA 下真实值很可能为 0 [INFERRED 低置信]，不建议盲试。
- **+0x120 11-bit 完整语义表**：DDK 源 bit 位置 [MEASURED]；bit0=1 [INFERRED 高置信]；其余十位 DDK 取值 [UNKNOWN]；`0x1` 充分性 [UNCONFIRMED]。
- **Early-out bit4+bit5** [MEASURED]：全置时跳过 Header 写入；语义 [INFERRED 中置信] 为"无 TA 工作"。
- **超时根因仍未知**：r451 已证实 13 BO 绑定、context READY 但固件无响应；可能需检查 TA 命令流或 Bridge 参数。
- 纯离线轮，零硬件触碰，无生产代码变更。
- 报告：mt-vgpu-guest/reports/r452-pskickta-flags-plus68-plus120.md
