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
> r374 轮按 §4 清理：r369 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## 本轮进展（r374：R5 per-file GPU VM/BO 后端设计定稿）

- **设计**：TA 真实渲染 payload 的内存路径——per-file GPU VM（真实设备 store，非 `store=file` facade）+ `mt_bo_system_borrow()` 借入 TA 命令缓冲 + 预留 VA（`0x70000000`）绑定 + `sealed`/`uploaded` 校验门（`MT_TA_VM_READY`，照抄 TQX）。
- **映射流程 8 步**：pin userspace 页 → 构造 `mt_system_memory` → borrow 进设备 store → bind 进 per-file VM → seal/upload 页表 → `gpu_va` 填包 → 完成时 unpin+释放 BO（VM 保留复用）。
- **接口**：`kernel/mt_ta_vm.h`（`struct mt_ta_vm_context`、`struct mt_ta_cmd_mapping`、静态断言，无实现）；门禁测试 `tests/test_ta_vm_layout.py`（6 tests，反向验证通过）。
- **状态标注**：borrow/sealed 门/facade 不可用均为 [MEASURED]；VA base 与流程为 [INFERRED]；V1–V4（VA 接受性/页表正确性/双上下文隔离/关闭清理）待活体。
- **门禁**：`check-offline` 425 Python + 299 C 全绿；纯设计轮，未跑 `make kernel`（无实现代码）。
- 报告：`reports/r374-perfile-gpu-vm-design.md`；证据 `r374-inventory.txt`（0600）。

## 本轮进展（r373：OUT.update_fence 回填验证通过）

- **根因**：`OUT.update_fence` 内核回填路径一直正常——`pvr_cmd_musakickgfx2()` 的 `out.update_fence=(int)wire_id`、`pvr_out()` 的 `copy_to_user`、12B OUT 结构体（`error@0`/`update_fence@4`/`update_fence_3d@8`）与 KMD 5.2.0 生成头完全一致。r372 的 "userspace 读到 0" 系其一次性 harness 传参 bug（`out_ptr`/`out_size` 未正确设置）。
- **活体验证**（bridge 未重载）：自写 Python harness 直调 ioctl，两次 TA-only kick 均精确匹配——`OUT.update_fence=3` vs dmesg `wire=3`，`OUT.update_fence=4` vs dmesg `wire=4`。
- **修复**：`pvr_out` 失败时改记 `pr_warn`（"OUT writeback failed rc=%d wire=%u"）并返回错误码，不再先记误导性的 "submitted wire" info 日志。此前 dmesg 看似成功、userspace 实际拿错误码，正是 r372 被误导的根因。
- **门禁**：`check-offline` 425 Python + 299 C 全绿（新增 `tests/test_ta_kick_out_writeback.py` 4 tests）；`make kernel` W=1 零警告；反向验证通过（回退→3/4 红，恢复→绿）。
- 报告：`reports/r373-ta-kick-out-writeback-verified.md`；证据 `r373-live-out-writeback.txt`（0600）。

## 本轮进展（r371：端到端 TA 验证被固件 trial 状态阻塞）

- 冷启动后 `0x890=2`（固件启动即为 2，非残留）；probe 改源码 3 处接受 `0x890==2`（`trial_connect=Y` 时），绑定成功。
- 但 trial 无法启动：`mt_trial_start` 要求 `0x890==0`，`connect_result=-61`，`pinned=0`；固件 MMIO `0x890=2` vs RPC `fw_state=1` 不一致。
- TA kick dispatch 到达桥侧（dmesg 解码日志），但 `pvr_session_acquire` 返 `-ENODEV`（trial 未 pinned）；r370 完成路径未被活体执行。
- 门禁 417+299 全绿，`make kernel` W=1 零警告；报告 `r371-ta-e2e-blocked-by-trial.md` 入库。

## 本轮进展（r370：生产 TA 完成路径已实现，活体被 EHOSTDOWN 阻塞）

- **实现**：`mt_pvr_bridge.c` 新增 `pvr_ta_wait_complete()`（轮询 DM3 等 0x100，2s 超时）与 `pvr_ta_abandon()`（超时以 `-ETIMEDOUT` error-signal fence），接入 `pvr_cmd_musakickgfx2()` 成功路径。**不碰 frozen probe**（其 `mt_runtime_event` 为 static，`mt_marker_complete` 为旧编译副本）。
- **门禁**：`check-offline` 417 Python + 299 C 全绿；`make kernel` W=1 零警告；新增 `tests/test_ta_completion_path.py`（6 tests）+ 反向验证通过。
- **重载**：一次计划内重载成功（r360 流程），refs probe=1/bridge=0，dmesg 干净，freeze 完好。
- **阻塞**：TA kick dispatch 到达，但 `submit_ta_work` 返 `-EHOSTDOWN`——`mt_runtime_can_submit`（probe 内）拒绝，`pvr_session_acquire` 成功故 trial.pinned/connected 为真，卡点在余下条件之一。**环境/trial 状态问题，非 r370 代码所致**。新代码未被活体执行。
- 报告：`mt-vgpu-guest/reports/r370-ta-completion-path-live-blocked.md`；证据 `r370-dmesg.txt`（0600）。
