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

## r437 (2026-10-09): +0x28/+0x30 单 RT 恒为 0，MLIST VA 不进 kick 路径（离线反汇编）

- 终局结论 [MEASURED]：`TA_buf+0x28`/`+0x30` 在单 RT 下恒为 0——
  `local_5b0+0x68`/`+0x80` 即栈局部 `local_548`/`local_530`
  （rbp 偏移恒等式：rbp-0x5b0+0x68=rbp-0x548），在
  `RGXAddRenderTarget:49349-49350`（`local_62c < 2` 单 RT 分支）赋 0，
  全函数 49000–50000 无其他赋值点；我方置零与 UMD 完全一致，
  r432/r436 超时与此二字段无关。
- MLIST VA（`local_558`，decompiled.c:49222 唯一赋值）在 49000–49900
  零读取——分配后即丢弃，不进 TA Header/psKickTA 构建；
  r433 的"MLIST VA 首要候选 [INFERRED]"被证伪。
- psKickTA[3]/[10] = 0（单 RT）。
- r438 前置（离线）：+0x68 布尔（UMD 写 `(*param_2&3)==3`，我方写 0）、
  单 RT 下 RGXPrepareTA 完整写入清单逐项对照、RgnHeader 内容、DM 包本身；
  P0 完成前不得活体。
- 门禁 `check-offline` 全绿（554 Python + 1491 C）；无代码变更，未跑
  `make kernel`；零硬件触碰，纯离线。

## r436 (2026-10-09): RgnHeader fill-1 live -- firmware still 5s timeout (highest-risk)

- 6th cold reboot live: dual-gate build (W=1 zero warnings), T1-T5 pass
  (554 Python + 1491 C).
- Trial rebuild lesson: runtime_context=0 clean trial (pinned=0) caused bridge
  0x82:0x12 -ENODEV -- pvr_session_acquire() requires trial.pinned AND
  trial.connected; reloaded probe with runtime_context=1 ->
  pinned=1 connected=1 (Guest/FW 2/2, matches r432 session state).
- Full chain: connect -> ctx 0x1000 -> 13th RgnHeader BO (va=0x7c000000,
  per-dword fill 1, dmesg confirms binding) -> 0xFD submit (buf+0x10=0x7c000000)
  -> fence allocated -> 5s timeout (errno=110).
- r433/r434 fill correction FALSIFIED as root cause by live evidence
  (0xFF -> 1 did not change behavior).
- Compare: r414 (all-zero, 219us no-work) / r425 (pixel BO, timeout) /
  r432 (RgnHeader fill 0xFF, timeout) / r436 (RgnHeader fill 1, timeout).
- Teardown: pending fence -> bridge ref=1, safe_rmmod.sh correctly refused;
  dmesg zero WARN/BUG/Oops; awaiting user 7th cold reboot.
- Source reverted, default rebuild W=1 zero warnings, tree clean.
- Honest boundary: RgnHeader still INFERRED; next MUST be offline on
  +0x28/+0x30 (MLIST VA candidate). No more live probing without basis.

