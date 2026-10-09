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
> r418 轮按 §4 清理：r415 节已移入归档。


## r418 (2026-10-09): 双门控回读活体——路径通、ABI bug 修复、固件超时

r418（最高风险轮，活体）：双门控构建（MT_TA_READBACK_DEBUG=1 + MT_TA_REAL_PACKET=1，intentional static_assert 临时中和，已还原）`make kernel` W=1 零警告；pre-live T1/T2/T3 全过；`insmod` 新桥后跑 `mt-ta-readback`——首轮 `pvr_in` 报 -EINVAL，dmesg 调试定位到 ABI bug：`struct mt_pvr_ta_readback_in` 内核侧未 packed（24B）vs userspace packed（20B），r416/r417 离线测试未捕获；修复 1 行（`__attribute__((packed))`）后 0xFD 全路径执行，`mt_ta_submit_real` 成功、fence 分配，但固件 5s 超时（ETIMEDOUT，submitted-but-ignored）；r414 同结构 TA（Q0=0）219µs 完成，本轮 Q0=`va|0x48000000000`（[INFERRED]）后超时——Q0 编码很可能不对，不做盲探；12th target BO 活体绑定确认（0x7b000000，16KB）；pending TA fence 致 bridge ref=1，`safe_rmmod.sh` 正确拒绝未强卸，待用户冷重启；门控/断言已 revert，源码树仅保留 packed 修复；门禁 522+625 全绿；本地提交未 push。诚实边界：Q0 仍 [INFERRED] 待离线深挖；像素未验证，T2 仍 open；生产零改动。

## r417 (2026-10-09): 回读路径测试加固——+19/+326 测试，两处门控 latent build break 修复

r417（离线，零硬件）：用户指示"稳妥推进 先加测试"。`tests/ta/test_ta_readback.py` +19（12th BO 生命周期 6：create 绑定位置/槽位 11 无冲突/destroy 逆序/bind 失败无泄漏/rollback 复用 destroy/VA 槽位表达式；0xFD 参数校验 6：坏 ctx→-EINVAL/未就绪→-ENODEV/参数透传/门控一致性/关门→-ENOTTY/前向声明；target_va 4：空指针/n_entries 越界/透传/staging BO 检查；像素分析 3：头文件存在/工具引用/ENOTTY 双门控提示）。`tests/c/pvr_bridge_core_test.c` +4 函数（+326 checks：entry 构建校验/Q0 位打包/buffer target_va/像素分析单元测试，含 black quirk/alpha 忽略/16 色 cap）。测试发现两处真实 latent build break 并修复：(1) 0xFD dispatch case 门控仅 DEBUG，handler 需 DEBUG&&REAL——(1,0) 报 implicit declaration，已改双门控；(2) `pvr_cmd_ta_readback` 调用 `mt_ta_submit_real` 早于其定义且无前向声明——(1,1) 历史从未编译成功，已补声明。新建 `userspace/ta_readback_analyze.h`（像素逻辑提取，行为锁定 r416）；`mt-ta-readback.c` 改用；ENOTTY 提示注明双门控。r415 的 2 个测试改锚定 `__maybe_unused` 定义。门禁 522+625 全绿，`make kernel` W=1 零警告；反向验证 4 项（TDD 红→绿、Q0 OR→XOR 捕获、像素 shift 破坏捕获、门控组合构建实证）。本地提交未 push。诚实边界：Q0 flag 仍 [INFERRED]；(1,*) 构建验证临时中和 intentional static_assert（已还原）；活体验证未做。

