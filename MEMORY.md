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
> r456 轮按 §4 清理：r454 节已移入归档。

## r456 (2026-10-09): 128 dwords RgnHeader 活体仍 5s 超时——r454"量不足"假说被证伪（第 15 次冷重启）

- **系统**：第 15 次冷重启（uptime 0 min），模块干净，HEAD `c1ca88b`（r455）。
- **Trial**：`fresh-trial.py --run --runtime-context` 成功（result=0, pinned=1, runtime_context_published=true）。
- **双门控**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和），userspace n_entries 1→0；`make kernel` W=1 零警告；门禁 596/598 通过（2 个 gate_default_off 预期失败）；构建后已 revert。
- **活体**：`mt-ta-readback /dev/dri/card1` → `check failed line 161: 0 errno=110`（ETIMEDOUT）；13 BO 绑定（RgnHeader at 0x7c000000），render context READY，固件无响应。
- **证伪**：r454"初始化量不足"假说被活体证伪。r453–r456 假说链：r453"未填真实数据"→r454 证伪；r454"量不足一半"→r456 证伪。**RgnHeader 方向已穷尽**。
- **安全**：bridge ref=1（pending TA fence，r440/r451 同模式），按协议停止未卸载；dmesg 零 WARN/BUG/Oops；未用 `rmmod -f`；未自行重启。
- 下一步：P0 用户第 16 次冷重启；P1 候选方向为 0x50B 包 opcode（0x66 vs 0x2ABC0065）[TO-VALIDATE] 或 Bridge 参数缺失；不建议继续 RgnHeader。
- 报告：mt-vgpu-guest/reports/r456-128dwords-still-timeout-falsified.md

## r455 (2026-10-09): RgnHeader 双循环初始化已实现——128 dwords 对齐 UMD（离线）

- **选项决策**：采用"选项 A 精炼版"——保留 `MT_TA_RGNHEADER_BYTES=0x100U`（逻辑尺寸，
  `mt_ta_rgnheader_size(64,64)==MT_TA_RGNHEADER_BYTES` 不变量不受影响），新增
  `MT_TA_RGNHEADER_INIT_BYTES=(2U*MT_TA_RGNHEADER_BYTES)`（0x200U）。UMD 两次循环写向
  同一 advancing pointer（连续内存），单循环写 128 dwords 功能等价。
- **实现**：`kernel/mt_ta_real.h` 新增 INIT_BYTES define（附 r454 注释）；
  `kernel/recovery/mt_pvr_bridge.c` 四处改用 INIT_BYTES（alloc/栈缓冲/填充循环/BO 写，
  64→128 dwords）；附带修复 `"0x100B"` 注释笔误。
- **测试**：C 新增 `test_ta_rgnheader_init_bytes` + 更新 `test_ta_rgnheader_init_pattern`；
  Python 新增 `tests/guest/test_rgnheader_double_init.py`（9 tests：define 关系/bridge 四处/旧模式清除）。
- **反向验证**：回退 kernel 后 8/9 精确 FAIL（第 9 个验证逻辑尺寸不变，旧代码本就通过）；
  恢复后 9/9 通过。
- **门禁**：`check-offline` 598 Python + C 全绿（851 checks）；`make kernel` W=1 零警告。
- 纯离线轮，零硬件触碰。下一步 r456（待第 15 次冷重启）：trial + 128-dword RgnHeader 活体。
- 报告：mt-vgpu-guest/reports/r455-rgnheader-double-init-implemented.md

