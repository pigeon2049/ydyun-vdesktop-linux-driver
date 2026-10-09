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
> r419 轮按 §4 清理：r419 节补入（前轮遗漏）。
> r420 轮按 §4 清理：r418、r417 节已移入归档。

## r420 (2026-10-09): Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）

r420（离线，零硬件）：用户指示"继续 加更多测试和门禁"。新增 21 Python 测试：`tests/ta/test_q0_purity.py`（T4 门禁，3 tests：Q0 禁止 OR/address 源码扫描、常量仅 bits 39/42、Q1 必须接 VA）、`tests/ta/test_q0_q1_bitfields.py`（15 tests：Q0 flag 位独立、低 32 位禁区、Q1 48 位 mask 边界、三态历史 r414/r418/r419）、`tests/ta/test_ta_real.py::TestTaDmLayoutUsage`（3 tests：常量被使用、禁硬编码 0x28/0x30、注释 [MEASURED]）。修复 1 处 stale 文档：`mt_marker_fence.h` 的 [INFERRED] 注释更新为 [MEASURED]（r414）。T4 反向验证：注入 r418 污染 → FAIL（定位行号）；还原 → 绿。门禁 543+299 全绿（Python，1 skipped）、630 C 全绿；`make kernel` W=1 零警告。本地提交未 push。诚实边界：Q0 flag 语义（除 29/30/39/42）仍未知；Q1=target 待活体验。

## r419 (2026-10-09): Q0 是纯 flags、地址在 Q1（离线反汇编）

r419（离线反汇编，零硬件）：r418 活体 Q0=`va|0x48000000000` 致固件超时，r414 Q0=0 曾 219us 成功。深挖 FUN_00169240：Q0 初始构造 `(sVar10<<4)<<48|(1<<61)` 无地址位（:44213）；Path B `uVar15|(prev&mask)|0x48000000000` 纯 flags carry-forward（:44317）；Q1 低 48 位=`*(param_1+0x10)` 才是目标地址（:44300/44321）。**核心结论：Q0 是纯 flags/control 字，零地址位；48 位目标地址在 Q1。** r418 把 VA OR 进 Q0 污染 flags。修正 `mt_ta_entry_simple_set_target()`：Q0=`0x48000000000`（flags only），Q1=`va & 0xFFFFFFFFFFFF`。位域：bits 39/42（0x48000000000，纠正 r410 的 43/46 笔误）、29/30 条件位、43-50 local_c4。门禁全绿，kernel 零警告。诚实边界：Q1=render target 为推断，待活体验。



## r418 (2026-10-09): 双门控回读活体——路径通、ABI bug 修复、固件超时

r418（最高风险轮，活体）：双门控构建（MT_TA_READBACK_DEBUG=1 + MT_TA_REAL_PACKET=1，intentional static_assert 临时中和，已还原）`make kernel` W=1 零警告；pre-live T1/T2/T3 全过；`insmod` 新桥后跑 `mt-ta-readback`——首轮 `pvr_in` 报 -EINVAL，dmesg 调试定位到 ABI bug：`struct mt_pvr_ta_readback_in` 内核侧未 packed（24B）vs userspace packed（20B），r416/r417 离线测试未捕获；修复 1 行（`__attribute__((packed))`）后 0xFD 全路径执行，`mt_ta_submit_real` 成功、fence 分配，但固件 5s 超时（ETIMEDOUT，submitted-but-ignored）；r414 同结构 TA（Q0=0）219µs 完成，本轮 Q0=`va|0x48000000000`（[INFERRED]）后超时——Q0 编码很可能不对，不做盲探；12th target BO 活体绑定确认（0x7b000000，16KB）；pending TA fence 致 bridge ref=1，`safe_rmmod.sh` 正确拒绝未强卸，待用户冷重启；门控/断言已 revert，源码树仅保留 packed 修复；门禁 522+625 全绿；本地提交未 push。诚实边界：Q0 仍 [INFERRED] 待离线深挖；像素未验证，T2 仍 open；生产零改动。

## r417 (2026-10-09): 回读路径测试加固——+19/+326 测试，两处门控 latent build break 修复

r417（离线，零硬件）：用户指示"稳妥推进 先加测试"。`tests/ta/test_ta_readback.py` +19（12th BO 生命周期 6：create 绑定位置/槽位 11 无冲突/destroy 逆序/bind 失败无泄漏/rollback 复用 destroy/VA 槽位表达式；0xFD 参数校验 6：坏 ctx→-EINVAL/未就绪→-ENODEV/参数透传/门控一致性/关门→-ENOTTY/前向声明；target_va 4：空指针/n_entries 越界/透传/staging BO 检查；像素分析 3：头文件存在/工具引用/ENOTTY 双门控提示）。`tests/c/pvr_bridge_core_test.c` +4 函数（+326 checks：entry 构建校验/Q0 位打包/buffer target_va/像素分析单元测试，含 black quirk/alpha 忽略/16 色 cap）。测试发现两处真实 latent build break 并修复：(1) 0xFD dispatch case 门控仅 DEBUG，handler 需 DEBUG&&REAL——(1,0) 报 implicit declaration，已改双门控；(2) `pvr_cmd_ta_readback` 调用 `mt_ta_submit_real` 早于其定义且无前向声明——(1,1) 历史从未编译成功，已补声明。新建 `userspace/ta_readback_analyze.h`（像素逻辑提取，行为锁定 r416）；`mt-ta-readback.c` 改用；ENOTTY 提示注明双门控。r415 的 2 个测试改锚定 `__maybe_unused` 定义。门禁 522+625 全绿，`make kernel` W=1 零警告；反向验证 4 项（TDD 红→绿、Q0 OR→XOR 捕获、像素 shift 破坏捕获、门控组合构建实证）。本地提交未 push。诚实边界：Q0 flag 仍 [INFERRED]；(1,*) 构建验证临时中和 intentional static_assert（已还原）；活体验证未做。

