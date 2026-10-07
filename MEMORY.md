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

最后更新：2026-10-07（r222 非零 kick 双腿闭环，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r222：非零 kick 双腿闭环，批准执行）

- r221 两处证伪本轮闭环：handler 搬到 `0x2:0xa`（新宏，生成头同名）+ `if (nupdate)`→`if (ncheck)`；门禁改判（syncprimset/render2/fn57/MAPPING）+ translator 新增 wait 门控断言；双重复位验证；`check-offline` 318+292 全绿；`make kernel` 零警告。
- 活体（`translate_kick=1`）：Leg1 预置+匹配 0.045s 即过（`SetSyncPrim→0`，fence=4）；Leg2 失配 5.007s 后 UMD 37（等待真实，无 marker）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。双 trace 已入库。详见 `reports/r222-nonzerokick-closed.md`。
- **Freeze 已恢复。**值语义至此真闭环（r212 机械 → r221 证伪 → r222 双腿）。
- 遗留：update 非零腿；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r221：非零 kick 活体发现，批准执行）

- 活体推翻两个离线假设：① UMD `SetSyncPrim` 实际发 `0x2:0xa`（objdump 实锤 `mov $0xa,%edx`；Ghidra 伪 C 写错 fn id；真身是跳板）——r220 handler 挂错位置，下轮搬到 `0x2:0xa`；② check-only 翻译不等 UFO 值（value=1 vs PMR=0 一次通过，fence=3），源码系 `if (nupdate)` 门控（r174 引入，疑笔误），r212/r213 只证明机械不证明值匹配。详见 `reports/r221-nonzerokick-findings.md` + trace。
- 拆桥干净（probe 25→1），默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：SyncPrimSet 搬移 + `if (nupdate)` 修复各独立成轮；非零 kick 双腿复验待搬移后。USB 短页标题日期问题留待对应轮。

---
