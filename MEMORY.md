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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。




## 本轮进展（r363：0x82:0xC 活体 IN 参数观察成功）

- r362 修正（`CreateSyncPrim` 入口捕获 `$rsi`）后 TA 路径一次打通：`SyncPrimRef` 返回 0，`0x82:0xC` 到达桥侧 observer，268B IN 解码（`kick_ta=1/kick_pr=1/kick_3d=0`，`ta_cmd_size=360`，`client_ta_upd_count=1`）并返 `-ENOTTY`；未提交 GPU 工作。
- 新发现：GDB 内直接 `open()` 的 fd 必须再做 `ioctl(0x40046445)`（INIT），否则 dispatch 卡在 `srv_handle==0` → `-ENOTCONN`，observer 永不触发。
- freeze 完好（bridge ref 0、probe ref 1），dmesg 无新增 WARN/BUG/Oops；门禁 400+299 全绿。
---

## 本轮进展（r362：r361 描述子"不匹配"系 GDB 脚本读错位置）

- `CreateSyncPrim` 反汇编确认：描述子 `+0x18` = `RA_Alloc` 的 `puStack_70` 输出，写入 `*param_2`（`b10`）；r361 GDB 脚本在入口取 `$rdi`（param_1）、返回时读 `*(param_1)`——读错了位置。
- 实测（同一 harness，两次独立运行）：`*(param_2)` 处 `+0x18=<ptr>、+0x20=0`（= r352/r353）；`*(param_1)` 处 `+0x18=NULL、+0x20=<heap ptr>`（= r361 dump）。直接调用 `SyncPrimRef(*b10)` → 0。
- 定性：测量方法 bug，非输入/状态/布局问题。无需 fabricated 参数调整。r363 唯一前置：修正 GDB 脚本从 `$rsi` 取 param_2。零硬件触碰，freeze 完好。见 `reports/r362-desc-mismatch-root-cause.md` + 双证据（0600）。
---
