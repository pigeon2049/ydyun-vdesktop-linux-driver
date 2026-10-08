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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。




## 本轮进展（r364：TA firmware 提交通道设计）

- R4 设计：`mt_marker_ops` 新增独立 op `submit_ta_work`（与 `submit_tqx_work` 并列，不碰 TQX 路径）；TA 分配 DM3（dm=1 TQX、dm=2 3D 已占用）；firmware 命令 opcode 候选 `0x66`；`0x82:0xC` IN 解码为 `struct mt_ta_submit_params`（104B）。
- 接口头文件 `kernel/mt_ta_submit.h`（只含接口定义与静态断言，无实现逻辑）；DM/opcode 为推断、须活体验证（V1–V6 清单见报告）；`kick_pr` 语义未编造，标 TO-VALIDATE。
- 门禁：新增 `tests/test_ta_submit_layout.py`（尺寸/偏移/DM 不碰撞），反向验证通过；`check-offline` 402+299 全绿，`make kernel` W=1 零警告。
- 全程离线：未加载模块、未提交 GPU 工作、freeze 完好。见 `reports/r364-ta-submit-channel-design.md` + 盘点表证据（0600）。
---

## 本轮进展（r363：0x82:0xC 活体 IN 参数观察成功）

- r362 修正（`CreateSyncPrim` 入口捕获 `$rsi`）后 TA 路径一次打通：`SyncPrimRef` 返回 0，`0x82:0xC` 到达桥侧 observer，268B IN 解码（`kick_ta=1/kick_pr=1/kick_3d=0`，`ta_cmd_size=360`，`client_ta_upd_count=1`）并返 `-ENOTTY`；未提交 GPU 工作。
- 新发现：GDB 内直接 `open()` 的 fd 必须再做 `ioctl(0x40046445)`（INIT），否则 dispatch 卡在 `srv_handle==0` → `-ENOTCONN`，observer 永不触发。
- freeze 完好（bridge ref 0、probe ref 1），dmesg 无新增 WARN/BUG/Oops；门禁 400+299 全绿。
---
