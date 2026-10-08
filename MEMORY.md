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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## 本轮进展（r368：wire 6 悬挂只读诊断；r367 所述 UAF 经代码证伪）

- **只读诊断**——wire 6 自 dmesg `[26595.505338]` 悬挂 ~940s 无完成事件；桥 refcnt=1（marker pinning，by design），probe ref=1；在载桥 build `4a78b331`；dmesg 零 oops/WARN。本轮零硬件碰触。
- **UAF 证伪**——TA op（`mt_marker_submit_ta_work`，r366 起未变）从未写 `m->context`（`m->context = c` 只存在于 TQX/context 两个无关 op）；`m` 为 kzalloc，`m->context` 恒为 NULL；`mt_marker_complete_ta` 的 `if (m->context)` 恒为假——**在载桥 `kfree(ctx)` 为干净释放，无 UAF 风险**（r367 系误将 TQX op 模式套用于 TA op）。
- **r367修复实引入泄漏**——去 kfree 后 `ctx` 无人接管（op 明确 "no context ownership"），每成功 dispatch 泄漏约 64B；所谓"`m->context` 未释放"不存在（恒 NULL）。
- **wire 6 根因**——生产事件路径（probe `mt_runtime_event` → 通用 `mt_marker_complete`）拒收 `0x100`；`mt_marker_complete_ta` 生产零调用（仅 r366 测试模块）；**无超时机制** → 永久悬挂。次生风险：`0x100` 事件可毒化 DM3 事件队列（drain 在 -EOPNOTSUPP 处 break 不推进 tail）。
- **恢复方案**（待确认后执行）——`mt_drain_pending.ko`（已构建，vermagic 匹配）清 wire 6 → refcnt 归 0 → `rmmod` → **回退 kfree 删除**（恢复干净释放）→ 按 r360 流程重载 → L3 复绿。r369+ 需补齐生产 TA 完成路径，否则后续 TA marker 重演悬挂。
- 见 `reports/r368-wire6-uaf-reassessment.md` + 双证据（0600）；门禁 `check-offline` 全绿。
---

## 本轮进展（r365：DM3 接受 opcode 0x66 marker，真机活体）

- 单发 TA marker（DM=3，opcode 0x66，空 payload）：firmware 即时消费，回 wire_id 匹配事件（words[1]=0x100，非 FAULT/超时/无视）；对照组 opcode 0x64 得标准 COMPLETE（words[1]=0）→ firmware 在分发层区分 opcode。V1（DM3）/V2（0x66）通过，无需 DM=4 回退。
- 探针 `mt_live_ta_marker.c`（一次性，未入库；build/traces/r365/，W=1 零警告）：raw queue 直发、不碰 marker store bookkeeping、事件只 peek 不 ack；两次 insmod/rmmod 均干净。
- refs 1/0 不变，dmesg 无新增 WARN/BUG/Oops；未跑 make probe（WITH_BRIDGE 会 rmmod，同 r358 取舍）。见 `reports/r365-ta-marker-dm3-opcode66-accepted.md` + 三证据（0600）。
---

