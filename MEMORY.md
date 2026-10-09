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
> r457 轮按 §4 清理：r455 节已移入归档。

## r458 (2026-10-09): TA 包 VM 信息修复实现（+0x18 root_pa、+0x20 token）（离线）

- **实现**：`struct mt_ta_submit_params` 新增 `u64 vm_root_pa`/`u64 vm_token`
  （`kernel/mt_ta_submit.h`，sizeof 104→120，pins 更新，ABI append）；
  `mt_ta_submit_real` 从 `rctx->exec_ctx_ta.process` 填写
  （`vm->tables->backing.gpu_pa` / `token`，同 `mt_execution_context_inputs` 表达式）；
  `mt_fw_ta_real_command` 新增 `root_pa`/`token` 参数，写包 `+0x18`/`+0x20`
  （`mt_fw_put64`，对齐 `mt_work_command_encode` 参考布局）；
  `mt_ta_submit_build` 透传；0x82:0xC 观察者路径（`mt_ta_params_from_musakickgfx2`）保持 0。
- **测试**：`tests/ta/test_ta_vm_info.py` 新增 11 tests（字段/赋值点/包写入/透传/观察者路径）；
  `tests/ta/test_ta_submit_layout.py` 扩展（sizeof 120，新增 offsets 104/112）。
- **反向验证**：新代码 11/11；回退 kernel 改动 10/11 精确 FAIL
  （第 11 个验证观察者路径保持 0，旧代码本就满足，符合预期）；恢复后 11/11。
- **门禁**：`check-offline` 609 Python OK（598+11）+ pvr_bridge_core_test OK (851 checks)；
  `kernel` W=1 零警告。
- **诚实边界**：[INFERRED 高] 缺失 VM 信息→MMU fault→hang 待 r459 活体验；
  若为根因，应看到行为变化（完成或新错误码，非 5s 超时）。
- 纯离线轮，零硬件触碰；**未链入下一轮**。
- 报告：mt-vgpu-guest/reports/r458-ta-packet-vm-info-implemented.md

## r457 (2026-10-09): TA 包缺失 VM 信息（+0x18/+0x20）——最可能根因，opcode 0x66 低嫌疑（离线）

- **核心发现** [MEASURED]：参考包构造器 `mt_work_command_encode()`（`kernel/mt_work_command.h`）
  填写 +0x18 root_pa（`vm->tables->backing.gpu_pa`）与 +0x20 process_id（`process->token`）；
  工作路径（TQX/3D）经 `mt_execution_context_inputs()` 填写。TA 路径的
  `mt_fw_ta_real_command()`（`kernel/mt_marker_fence.h:334`）只写 +0x0c/+0x28/+0x30/+0x48/+0x4c，
  **+0x18/+0x20 留零**。包构造钩子 `build_command(packet, wire_id, pid, params)` 签名无
  context 参数，`work.context = &rctx->exec_ctx_ta` 只用于 dm 校验与 fence 归属。
- **根因推断** [INFERRED 高]：TA VA 与 RgnHeader VA（0x7c000000）为 guest GPU 虚地址；
  无 root_pa 则固件/host 无法翻译 → GPU MMU fault → hang。吻合零/非零行为差异
  （r414 零 header 不解引用 VA → 219µs；r418+ 非零 → 解引用 → fault → 5s 超时）。
- **opcode 0x66** [INFERRED 低嫌疑]：KCCB（0x2ABC0065）与 DM 队列是不同接口，直接对比是范畴错误；
  proxy 对 0x66 包返回 TA 类完成码 0x100（`MT_FW_TA_COMPLETE_CODE`），识别为 TA；
  错 opcode 应立即错误而非 5s 超时。D4 的 TO-VALIDATE 保留。
- **Bridge 参数缺失** [INFERRED 中]：真实 `PVRSRVRGXKickTA3DKM` 约 40 参数
  （psKMHWRTDataSet/ZSBuffer/sync prims/PR fence/draw counts），我方仅填 ta_cmd_va/size；
  但属架构性绕行（直写 DM 队列），host 可能补足，无法从 guest 侧验证。
- **r458 修复方案**：`struct mt_ta_submit_params` 新增 `vm_root_pa`/`vm_token`；
  `mt_ta_submit_real` 从 `rctx->exec_ctx_ta.process` 填写（与 `mt_execution_context_inputs` 同表达式）；
  `mt_fw_ta_real_command` 新增参数写 +0x18/+0x20；`mt_ta_submit_build` 透传；
  0x82:0xC 路径保持 0。测试 + 反向验证 + 门禁。
- 纯离线轮，零硬件触碰，无生产代码变更。
- 报告：mt-vgpu-guest/reports/r457-ta-packet-missing-vm-info.md

## r456 (2026-10-09): 128 dwords RgnHeader 活体仍 5s 超时——r454"量不足"假说被证伪（第 15 次冷重启）

- **系统**：第 15 次冷重启（uptime 0 min），模块干净，HEAD `c1ca88b`（r455）。
- **Trial**：`fresh-trial.py --run --runtime-context` 成功（result=0, pinned=1, runtime_context_published=true）。
- **双门控**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和），userspace n_entries 1→0；`make kernel` W=1 零警告；门禁 596/598 通过（2 个 gate_default_off 预期失败）；构建后已 revert。
- **活体**：`mt-ta-readback /dev/dri/card1` → `check failed line 161: 0 errno=110`（ETIMEDOUT）；13 BO 绑定（RgnHeader at 0x7c000000），render context READY，固件无响应。
- **证伪**：r454"初始化量不足"假说被活体证伪。r453–r456 假说链：r453"未填真实数据"→r454 证伪；r454"量不足一半"→r456 证伪。**RgnHeader 方向已穷尽**。
- **安全**：bridge ref=1（pending TA fence，r440/r451 同模式），按协议停止未卸载；dmesg 零 WARN/BUG/Oops；未用 `rmmod -f`；未自行重启。
- 下一步：P0 用户第 16 次冷重启；P1 候选方向为 0x50B 包 opcode（0x66 vs 0x2ABC0065）[TO-VALIDATE] 或 Bridge 参数缺失；不建议继续 RgnHeader。
- 报告：mt-vgpu-guest/reports/r456-128dwords-still-timeout-falsified.md

