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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## r380 (2026-10-08): DM2/0x66 被 firmware 忽略，trial 会话被清除

- 单发 DM2 空 marker（opcode 0x66，r365 布局）：提交成功（wire=1），但 2 秒内无任何事件（`-ETIMEDOUT`）。Firmware 直接忽略，未返回完成/FAULT/NAK。
- **副作用**：被忽略的 marker 导致 firmware 清除 trial 会话（`0x890`: 2→0，`fw_state`: 2→0）；驱动 `trial.started` 仍为 1，形成不一致。
- 对照 r365（TA）：DM3/0x66 接受（`0x100`）、DM3/0x64 接受（标准 COMPLETE）。3D 路径行为显著不同。
- `0x66` 不是 3D 的有效 opcode（或 DM2 不接受最小 marker）。0x64 对照未测（trial 中断）。
- 安全：无 oops、无 hang；探针已卸载；bridge ref 0、probe ref 1 未动；本轮未重载模块。
- Trial 需冷重启恢复（warm reboot 不重置 firmware）。3D opcode 需从 Windows KMD 或完整 `submit_context` 路径研究，不宜在 trial 会话上试探。
- 报告 `reports/r380-dm2-opcode66-ignored.md`，门禁全绿，本地提交（未 push）。

## r379 (2026-10-08): 0x82:0x14 (MUSAKICKGFX5) 调研——现状 accept-and-log，执行路径设计完成
- 桥侧 `MT_PVR_FN_RGXKICKTA3D5` → `pvr_cmd_kickta3d5_observe()`（r215），解码 108B IN 后返回 0，**未真实执行**。
- Wire 结构已入库（`mt_pvr_wire.h:311`）：108B IN（render_context + check/update 数组 + submission_va@76/size@84 + counts），4B OUT（仅 error，**无 update_fence 回填**）。
- 与 0x82:0xC 关键差异：单一 submission（vs TA+PR+3D 三分路）、显式 render_context、无 fence 回填。
- 设计：DM2（3D 引擎，推断）、`mt_marker_ops` 第 6 op `submit_3d_work`、`submission_va` 经 R5 per-file VM 映射。
- 待验证 V1–V4：DM2 接受性、firmware opcode、完成事件格式、submission 解析。
- 门禁 425+299 全绿（无新增代码）。报告 `r379-82x14-musakickgfx5-research.md`。
