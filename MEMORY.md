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
> r459 轮按 §4 清理：r457 节已移入归档。
> r460 轮按 §4 清理：r458 节已移入归档。
> r461 轮按 §4 清理：r459 节已移入归档。

## r461 (2026-10-09): 证据优先级审计——Linux 优先未导致字段错误，但遗漏 KMD 层盲点（离线）

- **用户质疑**："是不是用同系列驱动优先级高过了 windows 驱动，导致部分实际上是错误的"
- **审计结论**：r430–r459 的 360B Header 字段结论使用 Linux UMD 是正确的——
  Windows mtdxum64.dll 根本没有 360B header/RgnHeader/psKickTA 概念
  （RGX 0 引用 vs Linux 643；QuYuan 0 vs 15；RgnHeader 0 引用）。
  **未发现字段级错误**。
- **发现的遗漏型错误**：
  - r453 "TA 命令流已排除"：未检查 Windows KMD 是否添加 TA 命令
  - r457 "包格式对比"：未检查 KMD 是否填充 VM 信息
- **最大代价**：KMD 层盲点直到 r460 才被识别（r430–r459 仅 r430 提及 Windows 一次）。
  若 r430 即对比三层模型，可能更早质疑直接 bridge 充分性。
- **需重新验证**：P0 反编译 mttkmd.sys（KMD kick 处理、RgnHeader 构造、VM 填充、
  最终固件命令格式）；P1 D3DDDI 同步语义；P2 直接提交路径。
- 纯离线轮，零硬件触碰，无生产代码变更。
- 报告：mt-vgpu-guest/reports/r461-evidence-priority-audit.md

## r460 (2026-10-09): Windows 驱动 TA 提交流程图/ER 图——D3D11 0x78B kick 路径基准（离线）

- **反编译源**：`mt-vgpu-guest/decompiled/mtdxum64.dll/decompiled.c`（7911 函数）；
  导出：`MtDxExtGetInterfaceImpl`、`OpenAdapter`、`OpenAdapter10`、`OpenAdapter10_2`。
- **流程**：D3D11 App → UMD DDI → `FUN_18021cf50` kick 数组构建器 →
  `FUN_180224220` 0x78B 条目构建器（magic `0x3089705f3089705f` at [1]）→
  D3DDDI 回调（`D3DDDIRenderCb` +0x78 / `D3DDDIEscapeCb` +0x90）→ KMD → 固件。
- **关键差异**：Windows 走 D3D11 DDI 三层模型（UMD→KMD→固件），Linux 走直连
  （UMD→Bridge→固件）；0x78B kick 与 360B Header 是不同抽象层，不可直接复制。
- **启示**：Windows 成功可能部分归功于 KMD 处理；我方直连模型需自己实现
  KMD 等效功能；D3DDDI Sync Object 语义值得对比研究。
- Mermaid 流程图 + ER 图已建立（见报告 §2/§4）。
- 纯离线轮，零硬件触碰，无生产代码变更。
- 报告：mt-vgpu-guest/reports/r460-windows-ta-flowchart-er-diagram.md

## r459 (2026-10-09): TA 包 VM 信息修复活体仍 5s 超时——MMU fault 假说被证伪（第 17 次冷重启）

- **Trial 前阻塞修复**：audit JSON module SHA 过期（r451 同模式），pahole 验证
  `mt_guest` 与 backup 一致、7 结构体无漂移后更新 SHA（保留此改进）。
- **Trial**：`shared channel round-trip result=0 mode=1 registered=15`；
  `firmware trial: connect=0 pinned=1 result=0` ✅。
- **双门控构建**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`，
  userspace `n_entries` 1→0；`make kernel` W=1 零警告；T1-T5 通过
  （2 个 gate_default_off 为 dual-gate 预期失败）；构建后 revert。
- **活体** ❌：`check failed line 161: 0 errno=110`（5s 超时）；
  13 BO 全部绑定，render context READY，固件无响应。
  **r457"MMU fault"假说被证伪**——修复后行为无任何变化。
- **假说证伪链**：r453（未填充）→ r454 证伪 → r454（量不足）→ r456 证伪 →
  r457（缺 VM 信息）→ **r459 证伪**；Header/包内容方向已穷尽。
- **安全**：bridge ref=1（r440/r451/r456 同模式），按协议停止；dmesg 零 WARN/BUG/Oops。
- **门禁**：609 Python OK + pvr_bridge_core_test OK (851 checks)；W=1 零警告。
- 报告：mt-vgpu-guest/reports/r459-vm-info-fix-still-timeout-falsified.md
- 证据：mt-vgpu-guest/build/traces/r459/dmesg-r459.txt（0600）

## r456 (2026-10-09): 128 dwords RgnHeader 活体仍 5s 超时——r454"量不足"假说被证伪（第 15 次冷重启）

- **系统**：第 15 次冷重启（uptime 0 min），模块干净，HEAD `c1ca88b`（r455）。
- **Trial**：`fresh-trial.py --run --runtime-context` 成功（result=0, pinned=1, runtime_context_published=true）。
- **双门控**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和），userspace n_entries 1→0；`make kernel` W=1 零警告；门禁 596/598 通过（2 个 gate_default_off 预期失败）；构建后已 revert。
- **活体**：`mt-ta-readback /dev/dri/card1` → `check failed line 161: 0 errno=110`（ETIMEDOUT）；13 BO 绑定（RgnHeader at 0x7c000000），render context READY，固件无响应。
- **证伪**：r454"初始化量不足"假说被活体证伪。r453–r456 假说链：r453"未填真实数据"→r454 证伪；r454"量不足一半"→r456 证伪。**RgnHeader 方向已穷尽**。
- **安全**：bridge ref=1（pending TA fence，r440/r451 同模式），按协议停止未卸载；dmesg 零 WARN/BUG/Oops；未用 `rmmod -f`；未自行重启。
- 下一步：P0 用户第 16 次冷重启；P1 候选方向为 0x50B 包 opcode（0x66 vs 0x2ABC0065）[TO-VALIDATE] 或 Bridge 参数缺失；不建议继续 RgnHeader。
- 报告：mt-vgpu-guest/reports/r456-128dwords-still-timeout-falsified.md

