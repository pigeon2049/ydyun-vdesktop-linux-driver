# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r189 对象查找去重；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r189：对象查找去重）

- 零硬件触碰。`pvr_object_find` 收敛 9 处重复查找；map 删锁内重复
 reservation 查找；另走查 connect/event/info/heap/pmr/open 等区域，
 结论均为不动。新增门禁 3 项 + 反向验证；全量 295+292 全绿，
 `W=1` 零警告。
- 证据：`reports/r189-object-find.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r188：保留项全抽取）

- 零硬件触碰。55 dispatch 标签命名进 wire.h（逐组计数替换，零残留）；
 PMR 三尺寸进 bridge 顶部；stream/slot 三宏进 addr_plan（含 readback
 除数）。未分发 ID 不命名。15 个旧门禁同步到宏形式；两处误伤已纠正。
 新增 fn_ids 门禁 2 项 + 反向验证；全量 292+292 全绿，`W=1` 零警告。
- 证据：`reports/r188-fn-table.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

