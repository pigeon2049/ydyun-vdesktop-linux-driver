# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r223 update 写回活体验证，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r223：update 写回活体验证，批准执行）

- translator 写回语义（r159）活体闭环：新工具 `pvr_update_writeback` 在 `translate_kick=1` 桥上，update-only fire 写 V=1 回 0，check kick 0.11ms 即时通过（时间即读回）；dmesg 双行 fence=5/6。工具零警告构建 + 5 项门禁（含反向）。详见 `reports/r223-update-writeback-live.md`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**`check-offline` 323 Python OK。
- 遗留：UMD 生成的真实 update 数组仍待 producer；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r222：非零 kick 双腿闭环，批准执行）

- r221 两处证伪本轮闭环：handler 搬到 `0x2:0xa`（新宏，生成头同名）+ `if (nupdate)`→`if (ncheck)`；门禁改判（syncprimset/render2/fn57/MAPPING）+ translator 新增 wait 门控断言；双重复位验证；`check-offline` 318+292 全绿；`make kernel` 零警告。
- 活体（`translate_kick=1`）：Leg1 预置+匹配 0.045s 即过（`SetSyncPrim→0`，fence=4）；Leg2 失配 5.007s 后 UMD 37（等待真实，无 marker）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。双 trace 已入库。详见 `reports/r222-nonzerokick-closed.md`。
- **Freeze 已恢复。**值语义至此真闭环（r212 机械 → r221 证伪 → r222 双腿）。
- 遗留：update 非零腿；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。

---
