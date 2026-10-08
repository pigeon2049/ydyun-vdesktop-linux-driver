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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## r379 (2026-10-08): 0x82:0x14 (MUSAKICKGFX5) 调研——现状 accept-and-log，执行路径设计完成
- 桥侧 `MT_PVR_FN_RGXKICKTA3D5` → `pvr_cmd_kickta3d5_observe()`（r215），解码 108B IN 后返回 0，**未真实执行**。
- Wire 结构已入库（`mt_pvr_wire.h:311`）：108B IN（render_context + check/update 数组 + submission_va@76/size@84 + counts），4B OUT（仅 error，**无 update_fence 回填**）。
- 与 0x82:0xC 关键差异：单一 submission（vs TA+PR+3D 三分路）、显式 render_context、无 fence 回填。
- 设计：DM2（3D 引擎，推断）、`mt_marker_ops` 第 6 op `submit_3d_work`、`submission_va` 经 R5 per-file VM 映射。
- 待验证 V1–V4：DM2 接受性、firmware opcode、完成事件格式、submission 解析。
- 门禁 425+299 全绿（无新增代码）。报告 `r379-82x14-musakickgfx5-research.md`。
## r378 (2026-10-08): 真实页表绑定验证通过（V2 非空，无 oops）
- 一次性内核模块 `mt_live_ta_bind`：正式 `mt_gpu_vm_init()` 创建 VM，1 真实页（pa=0x1688f8000）绑定到 VA `0x70000000`，`mt_gpu_vm_bind_many()` 返回 0，**无 oops**。
- r375 oops 根因彻底消除（proper init + ops 一致）。模块卸载干净，无泄漏；dmesg 无 WARN/BUG/Oops。
- 回归：marker 级 TA kick 正常（OUT.update_fence=3 == dmesg wire=3）。
- 诚实边界：firmware 侧 VA 翻译未验证（无查询接口）；`MT_TA_VM_READY` 门保持关闭。
- 门禁 425+299 全绿（无新增代码，仅文档证据）。本地提交待执行。
