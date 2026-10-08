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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r392 (2026-10-08): R6-5 调研结论——server 侧 CCB 不需要真实化（离线）

结论：0x88:0x5（BridgeRGXCreateKickSyncContext2）空 token 已足够，R6-5 关闭为 wont-do by design。CCB=命令环，两面：UMD 侧 SubmissionBufAllocator（用户态，render context +0x200，r199）+ server 侧设备内存 ring（Windows KMD 内部分配，r56 rung6 无 UMD 可见 PMR/heap）。本桥 DDK2 路径直接在内核构造固件包（DM3/0x66 r366、DM2/0x68 r382）经 DM 提交，不经 CCB ring；0x82:0xC / 0x82:0x14 IN 无 kicksync 字段；0x88:0x2/3/4 仅存在性校验。r144 活体证：harness 传参修正后 CCB 全生命周期（create→destroy）全 0、零崩溃——r143 崩溃是 harness bug 非桥缺口。与 R6 独立，不阻塞真实 UMD。

门禁：check-offline 全绿（纯调研无代码改动）。

报告 reports/r392-ccb-no-server-state-needed.md，证据 reports/r392-evidence.txt。
## r391 (2026-10-08): R6-4 Kick 侧 render_ctx 解析活体验证通过（V3 隔离）

**实现**：`pvr_cmd_musakickgfx2`（0x82:0xC）经 `pvr_object_find(file, in.h_render_context, MT_PVR_KIND_CONTEXT)` 解析 render_ctx；若 `resources_ready` 则 `kick_vm=rctx->vm`（per-context VM），否则回退 `file->ta_vm_ctx`（per-file，Phase 1 保护 marker）。V2 bind 验证跑在选中的 VM 上。TA marker 的 `work->context` 仍用 TA-dm（`render_ctx->exec_ctx` 为 node_type 5/DM3D，真实 exec 提交待 R6-5）；0x82:0x14 未动（仍 observer）。

**活体**：bridge 重载一次（r391 构建）。Marker 回归：no-ctx kick（fence=1，per-file）/ ctx1 kick（fence=2，per-context）/ ctx2 kick（fence=3，per-context）全部 error=0，OUT.update_fence 与 dmesg wire 匹配。V3：两 context 各持独立 `mt_bridge_ta_vm`（`vm_base_va` 同为 0x70000000 系设计——隔离在 VM/页表级，非 VA 级），kick 按 handle 正确路由，无串扰；文件关闭后 probe ref 25→13（r390 V2b 路径）。dmesg 零 WARN/BUG/Oops。

**门禁**：463 Python + 299 C 全绿（+5 新测试 `test_kick_render_ctx.py`）；`make kernel` W=1 零警告；反向验证通过。

**诚实边界**：`exec_ctx` 未用于 TA 提交（dm 不匹配，系设计）；VA 范围跨 context 重叠（页表隔离）；firmware 侧 VA 翻译仍 TO-VALIDATE。

报告 `reports/r391-kick-render-ctx-verified.md`，证据 `reports/r391-dmesg-kick-v3.txt`。

## r390 (2026-10-08): R6-3 Destroy 真实化活体验证通过（无泄漏）

**实现**：`mt_render_context_destroy()`（`kernel/recovery/mt_pvr_bridge.c`）——严格逆序释放：exec context → exec process（`vm->owners--`，须在 VM fini 前，否则 fini 返 `-EBUSY`）→ 11 BO `mt_bo_put`（VM 每 binding 持一 ref，`mt_gpu_vm_fini` 释放之）→ `mt_render_context_vm_destroy`（fini + `pt_bo` put，fini 失败加 `WARN_ON`）。`resources_ready=false` 的半初始化安全：`exec_ready`/`bos_ready[]`/`vm`-NULL 守卫；全状态清零，二次调用 no-op。Create 的 `out_rollback` 改调它（单一路径，删 15 行重复）。

**钩子**：`pvr_cmd_handle_release`（`0x82:0x13` + DDK2 destroy）在 `kind==MT_PVR_KIND_CONTEXT && obj->render_ctx` 时调 destroy + `kfree`；legacy 空 token 跳过。`pvr_file_release` 加 V4 文件关闭清理（否则 `rmmod` 泄漏 11 BO+VM+exec——r389 V1 遗留 probe ref 13 即此因）。

**活体 V2**：bridge 重载一次（r390 构建）。V2a 显式 destroy：create（ref 13→25）→ `0x82:0x13` destroy（ref 25→13），delta -12 全释放。V2b 文件关闭：create（→25）→ 直接 close fd（→13），`pvr_file_release` 清理生效。dmesg 零 WARN/BUG/Oops。

**门禁**：458 Python + 299 C 全绿（+8 新测试 `test_render_context_destroy.py`，r389 `test_rollback` 更新为委托检查）；`make kernel` W=1 零警告；反向验证通过。

**诚实边界**：probe ref 基线 13（含 r389 旧泄漏 12 refs，下次冷重启清零）；V2 delta 25→13 证明新路径无泄漏；半初始化 destroy 未活体 fault 注入（仅代码审查）；V3（多 context 隔离）、R6-4（kick 解析）待后续。

报告 `reports/r390-render-context-destroy-verified.md`，证据 `reports/r390-dmesg-v2.txt`。

