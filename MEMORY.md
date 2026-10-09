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

## r443 (2026-10-09): Trial 重建被 reserve_memory 阻塞（第 8 次冷重启后设备异常）

- 第 8 次冷重启确认（uptime 0 min，模块未加载）；`check-offline` 559+781 全绿；
  双门控测试构建 `make kernel` W=1 零警告（事后 revert）。
- **阻塞**：`mt_guest_probe` 在 `reserve_memory=1` 时 probe 失败 `-EINVAL`（-22）；
  `enable_probe/query_info/probe_rpc` 均成功，`reserve_memory` 为失败点（4 次复现）。
  `reserve_memory` 是 trial 链前置依赖 → 无 trial → 活体无法执行。
- 排查：双门控仅改 `mt_ta_real.h` defines + userspace；probe 源码未动；
  旧版 .ko 内核版本不匹配；设备 PCI 可见 [1ed5:0222]。
- 推测：第 8 次冷重启未完全重置固件/BAR 状态，或 VRAM 分配器遇设备侧异常。
- 安全：零 live 操作（未加载 bridge，未运行 readback）；源码已 revert；工作区干净；
  证据 `build/traces/r443/dmesg-r443.txt`（0600）。
- 下一步：用户确认完整冷重启（或第 9 次）后重开 trial 重建。

## r442 (2026-10-09): +0x120 flags 写入实现（离线）

- `mt_ta_real_buffer_build()` 追加 `*(u32 *)(buf + MT_TA_BUF_HDR_FLAGS) = MT_TA_BUF_HDR_FLAGS_MIN;`
  （`0x1`，r441 [MEASURED] UMD 忠实最小值：`+0x120` 4B flags dword 初始化 0 后 OR 入 11-bit
  打包；DDK flags 全零时仅 bit0=`(RTDataSet+0x00 & 2)==0` [INFERRED 高置信通常 1]）。
- 新增 `MT_TA_BUF_HDR_FLAGS 0x120U` / `MT_TA_BUF_HDR_FLAGS_MIN 0x1U`（附 11-bit 表摘要）；
  文档更新非零字段清单（`+0x10`、`+0x50`/`+0x58`、`+0x120`）。
- T5：白名单扩展至 `{0x10, 0x50, 0x58, 0x120}`；新增 `test_header_flags_constants` /
  `test_header_flags_value`；C `ta_hdr_written_byte` 加入 4B 范围，
  `test_ta_real_buffer_build_target` 断言 `+0x120 == 0x1`。
- 反向验证：`+0x120` 写回 0 → `test_header_flags_value` 精确 FAIL；还原 → 绿。
- 门禁 `check-offline` 全绿；`make kernel` W=1 零警告；零硬件触碰。
- 诚实边界：bit0=1 [INFERRED 高置信]；其余 10 bit DDK flags [UNKNOWN]——
  若固件需要某 DDK bit，`0x1` 仍不足，待活体验收；`+0x68` 仍 0（r438 P1）。

