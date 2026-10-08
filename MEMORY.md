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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r392 (2026-10-08): R6-5 调研结论——server 侧 CCB 不需要真实化（离线）

结论：0x88:0x5（BridgeRGXCreateKickSyncContext2）空 token 已足够，R6-5 关闭为 wont-do by design。CCB=命令环，两面：UMD 侧 SubmissionBufAllocator（用户态，render context +0x200，r199）+ server 侧设备内存 ring（Windows KMD 内部分配，r56 rung6 无 UMD 可见 PMR/heap）。本桥 DDK2 路径直接在内核构造固件包（DM3/0x66 r366、DM2/0x68 r382）经 DM 提交，不经 CCB ring；0x82:0xC / 0x82:0x14 IN 无 kicksync 字段；0x88:0x2/3/4 仅存在性校验。r144 活体证：harness 传参修正后 CCB 全生命周期（create→destroy）全 0、零崩溃——r143 崩溃是 harness bug 非桥缺口。与 R6 独立，不阻塞真实 UMD。

门禁：check-offline 全绿（纯调研无代码改动）。

报告 reports/r392-ccb-no-server-state-needed.md，证据 reports/r392-evidence.txt。
## r393 (2026-10-08): R6-6 调研结论——server 侧 TDM context 不需要真实化（离线）

结论：0x89:0x8（RGXTDMCreateTransferContext2）空 token 已足够，R6-6 关闭为 wont-do by design。TDM=Transfer Data Manager（2D/blit 引擎，KMD 头 common_musaxfer_bridge.h）。r150 活体证：真实 UMD 的 TDM 全生命周期（0x89:0x8 create → 0x89:0xa submit → 0x89:0x9 destroy）全 ret=0，UMD 正常推进；r174 活体：0x89:0xa accept-and-log 从真实 UMD 捕获到非零 CCB 字节。submit 仅做存在性校验（have_ctx），不读 context 状态；唯一真实的 TDM 资源是 shared-memory PMR（0x89:0x5，CLI+USC 独立，已实现）。与 R6 独立，不阻塞真实 UMD。若未来实现真实 TDM 执行可重开。

门禁：check-offline 全绿（纯调研无代码改动）。

报告 reports/r393-tdm-no-server-state-needed.md，证据 reports/r393-evidence.txt。

## r391 (2026-10-08): R6-4 Kick 侧 render_ctx 解析活体验证通过（V3 隔离）

**实现**：`pvr_cmd_musakickgfx2`（0x82:0xC）经 `pvr_object_find(file, in.h_render_context, MT_PVR_KIND_CONTEXT)` 解析 render_ctx；若 `resources_ready` 则 `kick_vm=rctx->vm`（per-context VM），否则回退 `file->ta_vm_ctx`（per-file，Phase 1 保护 marker）。V2 bind 验证跑在选中的 VM 上。TA marker 的 `work->context` 仍用 TA-dm（`render_ctx->exec_ctx` 为 node_type 5/DM3D，真实 exec 提交待 R6-5）；0x82:0x14 未动（仍 observer）。

**活体**：bridge 重载一次（r391 构建）。Marker 回归：no-ctx kick（fence=1，per-file）/ ctx1 kick（fence=2，per-context）/ ctx2 kick（fence=3，per-context）全部 error=0，OUT.update_fence 与 dmesg wire 匹配。V3：两 context 各持独立 `mt_bridge_ta_vm`（`vm_base_va` 同为 0x70000000 系设计——隔离在 VM/页表级，非 VA 级），kick 按 handle 正确路由，无串扰；文件关闭后 probe ref 25→13（r390 V2b 路径）。dmesg 零 WARN/BUG/Oops。

**门禁**：463 Python + 299 C 全绿（+5 新测试 `test_kick_render_ctx.py`）；`make kernel` W=1 零警告；反向验证通过。

**诚实边界**：`exec_ctx` 未用于 TA 提交（dm 不匹配，系设计）；VA 范围跨 context 重叠（页表隔离）；firmware 侧 VA 翻译仍 TO-VALIDATE。

报告 `reports/r391-kick-render-ctx-verified.md`，证据 `reports/r391-dmesg-kick-v3.txt`。

