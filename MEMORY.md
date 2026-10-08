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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## 本轮进展（r370：生产 TA 完成路径已实现，活体被 EHOSTDOWN 阻塞）

- **实现**：`mt_pvr_bridge.c` 新增 `pvr_ta_wait_complete()`（轮询 DM3 等 0x100，2s 超时）与 `pvr_ta_abandon()`（超时以 `-ETIMEDOUT` error-signal fence），接入 `pvr_cmd_musakickgfx2()` 成功路径。**不碰 frozen probe**（其 `mt_runtime_event` 为 static，`mt_marker_complete` 为旧编译副本）。
- **门禁**：`check-offline` 417 Python + 299 C 全绿；`make kernel` W=1 零警告；新增 `tests/test_ta_completion_path.py`（6 tests）+ 反向验证通过。
- **重载**：一次计划内重载成功（r360 流程），refs probe=1/bridge=0，dmesg 干净，freeze 完好。
- **阻塞**：TA kick dispatch 到达，但 `submit_ta_work` 返 `-EHOSTDOWN`——`mt_runtime_can_submit`（probe 内）拒绝，`pvr_session_acquire` 成功故 trial.pinned/connected 为真，卡点在余下条件之一。**环境/trial 状态问题，非 r370 代码所致**。新代码未被活体执行。
- 报告：`mt-vgpu-guest/reports/r370-ta-completion-path-live-blocked.md`；证据 `r370-dmesg.txt`（0600）。
## 本轮进展（r369：wire 6 已清除、kfree 回退上机，bridge 恢复 freeze）

- **恢复执行**——`mt_drain_pending.ko`（vermagic 匹配）insmod → `dm[3] count=1` → `drained=1 remaining_total=0`，bridge refcnt 1→0；确认后 rmmod drain 模块，无残留。
- **rmmod 桥**——回滚件 `build/traces/r369-recovery/mt_pvr_bridge.rollback-4a78b331.ko`（sha256 `1cc3d47f…`）；`rmmod mt_pvr_bridge` "unloaded cleanly"；probe ref=1 全程未动。
- **kfree 回退**——`pvr_cmd_musakickgfx2()` 成功路径恢复 `kfree(ctx)`，注释修正（r368 证伪 UAF：op 无 context ownership，释放为干净释放）；`make kernel` W=1 零警告；新桥（sha256 `4f5b08af…`）一次 insmod 成功，kallsyms 见导出，`/dev/dri/card1`+`renderD128` 正常。
- **门禁**——`check-offline` 411 Python + 299 C 全绿；新测试 `tests/test_ta_kick_ctx_release.py`（3 tests，反向验证：删 kfree→红，恢复→绿）。
- **健康**——dmesg 零 WARN/BUG/Oops；refs probe=1/bridge=0；freeze 恢复。见 `reports/r369-wire6-drained-kfree-restored.md` + 双证据（0600）。

## 本轮进展（r368：wire 6 悬挂只读诊断；r367 所述 UAF 经代码证伪）

- **只读诊断**——wire 6 自 dmesg `[26595.505338]` 悬挂 ~940s 无完成事件；桥 refcnt=1（marker pinning，by design），probe ref=1；在载桥 build `4a78b331`；dmesg 零 oops/WARN。本轮零硬件碰触。
- **UAF 证伪**——TA op（`mt_marker_submit_ta_work`，r366 起未变）从未写 `m->context`（`m->context = c` 只存在于 TQX/context 两个无关 op）；`m` 为 kzalloc，`m->context` 恒为 NULL；`mt_marker_complete_ta` 的 `if (m->context)` 恒为假——**在载桥 `kfree(ctx)` 为干净释放，无 UAF 风险**（r367 系误将 TQX op 模式套用于 TA op）。
- **r367修复实引入泄漏**——去 kfree 后 `ctx` 无人接管（op 明确 "no context ownership"），每成功 dispatch 泄漏约 64B；所谓"`m->context` 未释放"不存在（恒 NULL）。
- **wire 6 根因**——生产事件路径（probe `mt_runtime_event` → 通用 `mt_marker_complete`）拒收 `0x100`；`mt_marker_complete_ta` 生产零调用（仅 r366 测试模块）；**无超时机制** → 永久悬挂。次生风险：`0x100` 事件可毒化 DM3 事件队列（drain 在 -EOPNOTSUPP 处 break 不推进 tail）。
- **恢复方案**（待确认后执行）——`mt_drain_pending.ko`（已构建，vermagic 匹配）清 wire 6 → refcnt 归 0 → `rmmod` → **回退 kfree 删除**（恢复干净释放）→ 按 r360 流程重载 → L3 复绿。r369+ 需补齐生产 TA 完成路径，否则后续 TA marker 重演悬挂。
- 见 `reports/r368-wire6-uaf-reassessment.md` + 双证据（0600）；门禁 `check-offline` 全绿。
---
