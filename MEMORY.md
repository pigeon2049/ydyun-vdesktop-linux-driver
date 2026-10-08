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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。





## 本轮进展（r365：DM3 接受 opcode 0x66 marker，真机活体）

- 单发 TA marker（DM=3，opcode 0x66，空 payload）：firmware 即时消费，回 wire_id 匹配事件（words[1]=0x100，非 FAULT/超时/无视）；对照组 opcode 0x64 得标准 COMPLETE（words[1]=0）→ firmware 在分发层区分 opcode。V1（DM3）/V2（0x66）通过，无需 DM=4 回退。
- 探针 `mt_live_ta_marker.c`（一次性，未入库；build/traces/r365/，W=1 零警告）：raw queue 直发、不碰 marker store bookkeeping、事件只 peek 不 ack；两次 insmod/rmmod 均干净。
- refs 1/0 不变，dmesg 无新增 WARN/BUG/Oops；未跑 make probe（WITH_BRIDGE 会 rmmod，同 r358 取舍）。见 `reports/r365-ta-marker-dm3-opcode66-accepted.md` + 三证据（0600）。
---

## 本轮进展（r364：TA firmware 提交通道设计）

- R4 设计：`mt_marker_ops` 新增独立 op `submit_ta_work`（与 `submit_tqx_work` 并列，不碰 TQX 路径）；TA 分配 DM3（dm=1 TQX、dm=2 3D 已占用）；firmware 命令 opcode 候选 `0x66`；`0x82:0xC` IN 解码为 `struct mt_ta_submit_params`（104B）。
- 接口头文件 `kernel/mt_ta_submit.h`（只含接口定义与静态断言，无实现逻辑）；DM/opcode 为推断、须活体验证（V1–V6 清单见报告）；`kick_pr` 语义未编造，标 TO-VALIDATE。
- 门禁：新增 `tests/test_ta_submit_layout.py`（尺寸/偏移/DM 不碰撞），反向验证通过；`check-offline` 402+299 全绿，`make kernel` W=1 零警告。
- 全程离线：未加载模块、未提交 GPU 工作、freeze 完好。见 `reports/r364-ta-submit-channel-design.md` + 盘点表证据（0600）。
---

