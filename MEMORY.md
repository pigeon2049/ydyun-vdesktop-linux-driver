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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r390 (2026-10-08): R6-3 Destroy 真实化活体验证通过（无泄漏）

**实现**：`mt_render_context_destroy()`（`kernel/recovery/mt_pvr_bridge.c`）——严格逆序释放：exec context → exec process（`vm->owners--`，须在 VM fini 前，否则 fini 返 `-EBUSY`）→ 11 BO `mt_bo_put`（VM 每 binding 持一 ref，`mt_gpu_vm_fini` 释放之）→ `mt_render_context_vm_destroy`（fini + `pt_bo` put，fini 失败加 `WARN_ON`）。`resources_ready=false` 的半初始化安全：`exec_ready`/`bos_ready[]`/`vm`-NULL 守卫；全状态清零，二次调用 no-op。Create 的 `out_rollback` 改调它（单一路径，删 15 行重复）。

**钩子**：`pvr_cmd_handle_release`（`0x82:0x13` + DDK2 destroy）在 `kind==MT_PVR_KIND_CONTEXT && obj->render_ctx` 时调 destroy + `kfree`；legacy 空 token 跳过。`pvr_file_release` 加 V4 文件关闭清理（否则 `rmmod` 泄漏 11 BO+VM+exec——r389 V1 遗留 probe ref 13 即此因）。

**活体 V2**：bridge 重载一次（r390 构建）。V2a 显式 destroy：create（ref 13→25）→ `0x82:0x13` destroy（ref 25→13），delta -12 全释放。V2b 文件关闭：create（→25）→ 直接 close fd（→13），`pvr_file_release` 清理生效。dmesg 零 WARN/BUG/Oops。

**门禁**：458 Python + 299 C 全绿（+8 新测试 `test_render_context_destroy.py`，r389 `test_rollback` 更新为委托检查）；`make kernel` W=1 零警告；反向验证通过。

**诚实边界**：probe ref 基线 13（含 r389 旧泄漏 12 refs，下次冷重启清零）；V2 delta 25→13 证明新路径无泄漏；半初始化 destroy 未活体 fault 注入（仅代码审查）；V3（多 context 隔离）、R6-4（kick 解析）待后续。

报告 `reports/r390-render-context-destroy-verified.md`，证据 `reports/r390-dmesg-v2.txt`。

## r389 (2026-10-08): R6-2 Create 真实化活体验证通过（11 BO，无 oops）

**实现**：`mt_render_context_create()`（`kernel/recovery/mt_pvr_bridge.c`，~200 行）——8 步流程：per-context VM（`d->buffers`-backed 64KB 页表，解决 r376 synthetic VM 的 store/ops 不匹配）→ 11×（`mt_bo_create` + `pvr_translator_bo_write` 写初始数据 + `mt_gpu_vm_bind_many` 到 `0x70000000+i*16MB`）→ `mt_gfx_context_build_csw()` → `mt_execution_process_create` + `mt_execution_context_create(5,0)` → `resources_ready=true`；任一步失败逆序回滚。`pvr_cmd_render2_create`（`0x82:0x12`）改调它，不再 mint 空 token。`struct mt_pvr_render_context` 加 `pt_bo` 字段（1544B）。

**活体 V1**：单次 `0x82:0x12` → 11/11 BO 绑定成功、CSW built、exec created、`resources_ready=true`，dmesg 零 WARN/BUG/Oops；bridge 重载一次（r360 流程），probe 未动。实现中修正：16KB 页表仅容 2 ranges（ENOSPC）→ 增至 64KB。

**门禁**：450 Python + 299 C 全绿（+9 新测试 `test_render_context_create.py`）；`make kernel` W=1 零警告；反向验证通过。

**诚实边界**：仅 V1；R6-3（destroy）、R6-4（kick 解析）、V3/V4 待后续；probe ref 1→13（R6-3 释放）。

报告 `reports/r389-render-context-create-live-verified.md`，证据 `reports/r389-dmesg-v1.txt`。

## r388 (2026-10-08): R6-1 per-context 对象模型扩展落地（离线，纯结构，零硬件触碰）

**结构定义**：`kernel/mt_render_context.h` 新（45 行）——`struct mt_pvr_render_context` 1456B：11 BO（`struct mt_bo bos[11]`，968B）+ `vas[11]` + `bos_ready[11]` + `mt_execution_process`（32B）+ `mt_execution_context`（72B）+ `exec_ready` + `csw[248]` + `mt_bridge_ta_vm *vm`（前向声明）+ `vm_base_va` + `resources_ready`。`MT_RENDER_CONTEXT_VA_STRIDE` 16MB（r387 §3.3，TO-VALIDATE）。

**挂载**：`struct mt_pvr_object` 加 `struct mt_pvr_render_context *render_ctx`（:293-295，注释说明 NULL 语义）；`pvr_object_new()` 的 kzalloc 已保证 NULL 默认，无需改动。

**设计决策**（r387 §1.2 落实）：指针而非内嵌（避免 SYNC/PMR 等 kind 浪费 1456B）；独立头文件（userspace 可编译，前向声明 `mt_bridge_ta_vm`）。

**门禁**：441 Python + 299 C 全绿（新增 `tests/test_render_context_layout.py` 3 tests：layout pins/sizeof+offsets、render_ctx 存在性、kzalloc NULL 验证）；`make kernel` W=1 零警告；反向验证通过（stride 改 32MB → 1 failure）。

报告 `reports/r388-per-context-struct-defined.md`。R6-2（create 真实化）前置就绪。

