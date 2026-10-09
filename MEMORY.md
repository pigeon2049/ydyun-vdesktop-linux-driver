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
> r402 轮按 §4 清理：r400、r399 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。
> r404 轮按 \u00a74 清理：r402、r401 节已移入归档。
> r405 轮按 §4 清理：无（仅 r404、r405 两节，保留）。
> r407 轮按 §4 清理：r405 节已移入归档。
> r408 轮按 §4 清理：r406 节已移入归档。
> r409 轮按 §4 清理：r407 节已移入归档。
> r410 轮按 §4 清理：r408 节已移入归档。
> r412 轮按 §4 清理：r410 节已移入归档。
> r414 轮按 §4 清理：r411 节已移入归档。
> r415 轮按 §4 清理：r412 节已移入归档。
> r417 轮按 §4 清理：r414 节已移入归档。


## r417 (2026-10-09): 回读路径测试加固——+19/+326 测试，两处门控 latent build break 修复

r417（离线，零硬件）：用户指示"稳妥推进 先加测试"。`tests/ta/test_ta_readback.py` +19（12th BO 生命周期 6：create 绑定位置/槽位 11 无冲突/destroy 逆序/bind 失败无泄漏/rollback 复用 destroy/VA 槽位表达式；0xFD 参数校验 6：坏 ctx→-EINVAL/未就绪→-ENODEV/参数透传/门控一致性/关门→-ENOTTY/前向声明；target_va 4：空指针/n_entries 越界/透传/staging BO 检查；像素分析 3：头文件存在/工具引用/ENOTTY 双门控提示）。`tests/c/pvr_bridge_core_test.c` +4 函数（+326 checks：entry 构建校验/Q0 位打包/buffer target_va/像素分析单元测试，含 black quirk/alpha 忽略/16 色 cap）。测试发现两处真实 latent build break 并修复：(1) 0xFD dispatch case 门控仅 DEBUG，handler 需 DEBUG&&REAL——(1,0) 报 implicit declaration，已改双门控；(2) `pvr_cmd_ta_readback` 调用 `mt_ta_submit_real` 早于其定义且无前向声明——(1,1) 历史从未编译成功，已补声明。新建 `userspace/ta_readback_analyze.h`（像素逻辑提取，行为锁定 r416）；`mt-ta-readback.c` 改用；ENOTTY 提示注明双门控。r415 的 2 个测试改锚定 `__maybe_unused` 定义。门禁 522+625 全绿，`make kernel` W=1 零警告；反向验证 4 项（TDD 红→绿、Q0 OR→XOR 捕获、像素 shift 破坏捕获、门控组合构建实证）。本地提交未 push。诚实边界：Q0 flag 仍 [INFERRED]；(1,*) 构建验证临时中和 intentional static_assert（已还原）；活体验证未做。

## r415 (2026-10-09): 真实 TA 路径产品化——DM 布局固化 + mt_ta_submit_real() 落地

r415（离线）：DM 布局 VA @+0x28/size @+0x30 由 [INFERRED] 转 [MEASURED]（r414 活体 0x100/219us）；80B 包布局文档化。新生产函数 `mt_ta_submit_real()`（`kernel/recovery/mt_pvr_bridge.c`，`#if MT_TA_REAL_PACKET` 内）：r414 测试钩子重构为参数化 API（`struct mt_ta_real_request`{h_render_context,width,height,n_entries}），固定 64x64 hardcode 已移除，异步返 fence（调用方自行等待）；`mt_ta_real_buffer_build()` 纯函数（n 个 40B 条目）。BO[10]@4096 复用评估通过（VM seal 后无法新增绑定，r414 已验证；长期 fix 为 create 时专用 BO）。门控开启流程文档化（5 前置+开启步骤+回滚，marker 路径不受影响）。`0x82:0xFE` 钩子确认不在生产代码（从未提交）。`tests/ta/test_ta_real.py` +8（共 14）。门禁 488+299 全绿，`make kernel` W=1 零警告（门控开/关双路径），反向验证通过（门控置 1 → FAIL）。T2 白名单曾因注释引用宏名告警，已改为裸 0x66。本地提交未 push。

