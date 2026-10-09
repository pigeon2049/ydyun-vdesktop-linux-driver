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

## r435 (2026-10-09): 第 6 次冷重启未发生，停止活体（只读检查）

- 只读核查 [MEASURED]：启动 ~16:20:46 CST（dmesg -T 反推：17:43:24 − 4958s）；
  r432 活体 17:45:43（render context READY，13th rgnheader BO bound）在启动之后——
  **第 6 次冷重启未发生**。
- `mt_pvr_bridge` ref=1（r432 pending fence 遗留，safe_rmmod 已拒绝）；
  `mt_guest_probe` ref=1（正常）；残留完整 render context 未 teardown。
- 任务停止条件命中，**未执行任何活体操作**：未构建双门控、未重载 bridge、
  未跑 `mt-ta-readback`；未触碰残留会话。
- 门禁 `check-offline` **554 Python + 1491 C 全绿**；`make kernel` W=1 零警告。
- 证据 `mt-vgpu-guest/build/traces/r435/dmesg-r435.txt`（0600）。
- 下一步：用户执行第 6 次冷重启后重验（uptime/lsmod/dmesg），方可 r436 活体。
- 诚实边界：启动时间反推 ±2s；残留会话归属 r432 为 [INFERRED] 高置信；
  零硬件触碰；生产代码零变更。

## r434 (2026-10-09): RgnHeader 填充修正为逐 dword 写 0x00000001（离线）

- 落地 r433 纠正：`MT_TA_RGNHEADER_INIT_DWORD` 由 `0xFFFFFFFFU` → `0x1U`；
  `mt_render_context_create` 删除 `memset(rgn_init, 0xFF, ...)`，
  改为逐 dword 循环写 `1`（复用 `u32 i`，[MEASURED] r433）。
- 测试同步：`test_ta_rgnheader_init_pattern` 模拟逐 dword 写 1，
  断言 `== 0x1U` 且 `!= 0xFFFFFFFFU`（防 r431 重演）；
  `test_rgnheader_init_all_ones` 更新为 assertIn dword 循环 +
  assertNotIn memset 0xFF（首轮即精确拦截旧行为）。
- 反向验证：注入 `memset 0xFF` → 精确 FAIL；还原后全绿。
- 门禁 `check-offline` **554 Python + 1491 C 全绿**；
  `make kernel` W=1 **零警告**。
- 诚实边界：RgnHeader 语义仍 [INFERRED]；0x00000001 填充尚未活体验收
  （r435+，待用户冷重启）。零硬件触碰，纯离线。
