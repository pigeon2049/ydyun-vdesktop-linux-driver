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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r399 (2026-10-09): R5 Phase 2 活体验证--无 ctx kick 返回 -EINVAL（活体）

**验证**：pre-live 门禁 T1/T2/T3 全绿后，bridge 重载到 r398 构建
（safe_rmmod.sh，probe 不动）。V1：无 ctx 的 0x82:0xC kick 返回 -EINVAL
（errno 22，dmesg 有 r398 Phase 2 标记）；V2：0x82:0x12 create
（handle=0x1000）后带 ctx kick 成功，OUT.update_fence=10 对应 dmesg
wire=10 精确匹配；V3：destroy 后 probe ref 13->25->13，delta 归零无泄漏。

**门禁**：474+299 全绿；make kernel W=1 零警告；dmesg 零 WARN/BUG/Oops。
本轮无代码改动（纯活体验证）。

报告 reports/r399-perfile-removed-live-verified.md。

## r398 (2026-10-09): R5 Phase 2 完成——per-file VM 回退删除（离线）

**删除**：`file->ta_vm_ctx` 字段、前向声明、`pvr_file_release` 销毁块、r376
注释+defines、`mt_bridge_ta_vm_create/destroy`（synthetic BO）、`mt_ta_vm.h`
引入与头文件、两死亡测试。保留 `struct mt_bridge_ta_vm`（per-context 载体）。

**Kick 变更**：无有效 render_ctx（`resources_ready`+`vm`）时直接 `-EINVAL`
（`pvr_session_acquire` 之前，无锁）；有则对 `&rctx->vm->vm` 做 V2 空绑定。
删除 throwaway `kzalloc` 分支与 `use_real_ctx`；`ctx=&rctx->exec_ctx_ta`
恒为借用，删除两处条件 `kfree`。

**测试**：`test_kick_render_ctx`（fallback→`test_rejects_without_live_render_ctx`
断言 `-EINVAL`）、`test_probe_ta_vm`（Phase 2 语义）、`test_ta_kick_ctx_release`
（语义反转：借用 ctx 不得 kfree）、`test_ta_completion_path`（锚点改
`dma_fence_put`）。删除两死亡测试文件。

**门禁**：474+299 全绿；`make kernel` W=1 零警告；反向验证（注入旧引用→FAIL，
还原→全绿）。零硬件触碰；运行中 bridge 仍为 r397 构建，下次重载生效。

报告 reports/r398-per-file-vm-removed.md。

## r397 (2026-10-09): render_ctx 双执行上下文落地，kick 传真实 ctx（活体验证）

**实现**：`mt_render_context.h` 中 `exec_ctx`→`exec_ctx_3d` 改名，新增 `exec_ctx_ta`
（node_type=2→DM3）+ `exec_ta_ready`（struct 1544B→1616B）；create 第 7b 步建 TA ctx
（失败走统一 rollback）；destroy 先 TA 后 3D（顺序无关）；kick 有-context 传
`&rctx->exec_ctx_ta`（file->lock 下借用，marker 不拿所有权故行为中性），无-context
保留 throwaway + 条件 kfree。

**活体**（一次 bridge 重载，safe_rmmod.sh）：V1 双 ctx 创建成功
（`r397: exec process/contexts created (3D node_type=5, TA node_type=2)`）；marker 回归
KICK[ctx]→fence=6（`kick with real exec_ctx_ta (dm=3)`）、KICK[no-ctx]→fence=7，
OUT 与 wire 精确匹配；V3 双 context（0x1000/0x1001）各 kick→fence 8/9，
独立 VM 正确路由，文件关闭 ref 25→13 无泄漏；dmesg 零 WARN/BUG/Oops。

**门禁**：474+299 全绿（T2 新增 dual dm pinning：exec_ctx_ta node_type=2→DM3、
exec_ctx_3d node_type=5→DM2；反向验证通过）；kernel W=1 零警告。

**诚实边界**：marker 级；`exec_ctx_ta` 仅过门禁，未提交真实负载（Phase 3 远期）；
node_type=2 固件语义、in-flight destroy 待验证。

报告 reports/r397-dual-exec-ctx-live-verified.md。

