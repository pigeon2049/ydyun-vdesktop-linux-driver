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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## r382 (2026-10-08): submit_3d_work 落地（第 6 op，0x68，门控关闭，离线）

**结论**：submit_3d_work 作为 mt_marker_ops 第 6 个 op 落地（仿 r366 模式），
DM2/opcode 0x68 (RGXCompute)，完成码标准 0。门控 MT_3D_SUBMIT_GATE=0 默认关闭；
0x82:0x14 dispatch 保持 r215 observer。零硬件触碰。

**实现**：
- kernel/mt_3d_submit.h (新，108 行): MT_FW_DM_3D=2, MT_FW_3D_OPCODE=0x68,
  MT_FW_3D_COMPLETE_CODE=0, MT_3D_SUBMIT_GATE=0; struct mt_3d_submit_params (56B)
  含 vm_map_hook (R5 预留); mt_3d_params_from_rgxkickta3d5() 映射函数
- kernel/mt_marker_fence.h (+163): mt_3d_work, d3_params, 第 6 op (+ABI WARNING),
  mt_fw_3d_command() (0x68@0x0c, va@0x28, size@0x30), mt_marker_submit_3d_work()
  (门控关闭→-EOPNOTSUPP; 非空校验 r380 教训), ops 表, mt_bridge_submit_3d_work 声明
- kernel/recovery/mt_pvr_bridge.c (+9): mt_bridge_submit_3d_work + EXPORT_SYMBOL_GPL

**门禁**：check-offline 430+299 全绿 (+2 新测试); make kernel W=1 零新增警告
(4 pre-existing 来自 r376); 反向验证通过 (0x99→fail, 恢复→pass)。

**诚实边界**：未活体验证；dispatch 未切换；VM 映射未实现；门控开启待 r381 TO-VALIDATE。
见 reports/r382-submit-3d-work-offline.md。
## r381 (2026-10-08): 3D opcode 为 0x68 (RGXCompute)，0x66 在 DM2 仅对真实命令有效

**结论**：3D (DM2) 的 firmware opcode 是 **0x68** (RGXCompute, type 5)。0x66 在 DM2 上仅对真实命令包有效（mt_live_3d.c 实证，r37–r41），空 marker 被忽略（r380）。

**证据**：
- mt_work_opcode(): type 5 → 0x68 (RGXCompute), DM 2
- mt_live_3d.c: node_type=5 → DM2, req.type=3 → 0x66，真实命令成功
- Windows KMD (mtkm64.sys): 含 RGXCompute 字符串
- 完成码预测：标准 0（无 3D 特殊码定义）

**建议**：0x82:0x14 实现用 0x68；必须构造完整命令包，不得用空 marker；首次活体验证等真实 UMD 调用。

报告：mt-vgpu-guest/reports/r381-3d-opcode-0x68-rgxcompute.md

## r380 (2026-10-08): DM2/0x66 被 firmware 忽略，trial 会话被清除

- 单发 DM2 空 marker（opcode 0x66，r365 布局）：提交成功（wire=1），但 2 秒内无任何事件（`-ETIMEDOUT`）。Firmware 直接忽略，未返回完成/FAULT/NAK。
- **副作用**：被忽略的 marker 导致 firmware 清除 trial 会话（`0x890`: 2→0，`fw_state`: 2→0）；驱动 `trial.started` 仍为 1，形成不一致。
- 对照 r365（TA）：DM3/0x66 接受（`0x100`）、DM3/0x64 接受（标准 COMPLETE）。3D 路径行为显著不同。
- `0x66` 不是 3D 的有效 opcode（或 DM2 不接受最小 marker）。0x64 对照未测（trial 中断）。
- 安全：无 oops、无 hang；探针已卸载；bridge ref 0、probe ref 1 未动；本轮未重载模块。
- Trial 需冷重启恢复（warm reboot 不重置 firmware）。3D opcode 需从 Windows KMD 或完整 `submit_context` 路径研究，不宜在 trial 会话上试探。
- 报告 `reports/r380-dm2-opcode66-ignored.md`，门禁全绿，本地提交（未 push）。

