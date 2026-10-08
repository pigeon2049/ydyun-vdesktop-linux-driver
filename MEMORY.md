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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## r385 (2026-10-08): R7 Sync prim import 缺口分析（离线，零硬件触碰）

**R7 定义**（r355）：`ZeusSyncPrimImportFD` 下游在真实建连后应走通；桥侧 SYNC 命令多已实现。

**核心发现**（实测源码）：`ZeusSyncPrimImportFD` = SYNC:0xC（`BridgeSyncPrimImportFD`，IN 24B），但 Linux 桥侧**未实现**——`mt_pvr_wire.h` 无 0xC 的 `MT_PVR_FN_*` 定义，dispatch `default:` 返 `-ENOTTY`。

**SYNC 组现状**：0x0 Alloc（真实，`pvr_cmd_sync_block`）/ 0xA CpuSignal（真实，r222）/ 0x1/0x2/0x7/0x8 空桩（`pvr_stub_ok`）/ **0xC 缺失**。

**是否阻塞真实 UMD**：很可能阻塞 TA 路径——r361 证实 UMD 路径为 `...→ZeusSyncPrimImportFD→0x92930(0x82:0xC)`；`-ENOTTY` 可能致中止。但尚无活体证据（r361 在 `SyncPrimRef` 被阻塞）。

**与 R6 关系**：独立。R6=context 对象（firmware 状态），R7=sync prim 对象（FD 导入）；无依赖，但真实 UMD 渲染同时需要。

**缺口清单**：R7-1（0xC 常量定义）→ R7-2（dispatch handler）→ R7-3（IN/OUT 结构入库）→ R7-4（FD 导入语义设计）。

报告 `reports/r385-sync-prim-import-gaps.md`，证据 `reports/r385-evidence.txt`。门禁全绿。

## r384 (2026-10-08): R6 DDK2 context statefulness 缺口分析（离线，零硬件触碰）

**R6 定义**（r355）：`0x82:0x12`/`0x88:0x5` 从 handle token 到 server 侧状态（firmware context、CCB 管理）。

**现状**（实测源码）：三命令皆为 bookkeeping token，无 firmware 状态——
- `0x82:0x12`：`pvr_cmd_render2_create` mint `MT_PVR_KIND_CONTEXT`，IN（`priv_data`/`priority`）被 `(void)in` 丢弃
- `0x88:0x5`：`pvr_cmd_kicksyncctx2_create` mint `MT_PVR_KIND_KICKSYNC`，IN 8B opaque
- `0x89:0x8/0x9`：mint `MT_PVR_KIND_TDM_CONTEXT` 仅存 2 arg；destroy 仅 kfree token

**真实状态参照**（`mt_live_3d.c` 实证）：11 BO（86,300B，含 TA state）+ init data + GPU VA 绑定 + CSW（0xf8B）+ `mt_execution_context_create(node_type=5)`。

**关键张力**：R5 VM 是 per-**FILE**（`mt_pvr_file.ta_vm_ctx`），非 per-**CONTEXT**；真实 UMD 单 fd 可多 context，需决策路线（A: context 对象挂载状态 / B: per-file 扩展）。

**是否阻塞真实 UMD**：marker 级不阻塞（TA 忽略 handle，R5 够用）；真实 TA/3D 渲染**阻塞**（firmware 需 context BO+执行上下文）；多 context **阻塞**（状态串扰）。

**缺口清单**：R6-1 对象模型扩展 → R6-2 create 真实化（复用 `mt_live_3d.c`）→ R6-3 destroy 真实化 → R6-4 kick 侧解析 → R6-5 CCB（`0x88:0x5`）→ R6-6 TDM 调研。

报告 `reports/r384-ddk2-context-statefulness-gaps.md`，证据 `reports/r384-evidence.txt`。门禁全绿。

## r383 (2026-10-08): probe 侧 r376 死代码清理（离线，零警告）

- 删除 kernel/mt_probe_ta_vm.h（29 行，git rm）+ mt_guest_probe.c 中 122 行（struct mt_probe_ta_vm、4 函数、4x EXPORT_SYMBOL_GPL）；bridge 过时注释更新。
- grep 零引用（头文件无 include，函数无调用者）；nm 确认 ko 无残留符号；test_probe_ta_vm.py 保留作回归 guard。
- make kernel W=1 零警告（r376 的 4 个 pre-existing 警告消除）；check-offline 430+299 全绿。
- 未重载 probe/bridge，未重启；零硬件触碰。报告 reports/r383-probe-dead-code-removed.md。

