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

最后更新：2026-10-07（r221 非零 kick 活体发现，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r221：非零 kick 活体发现，批准执行）

- 活体推翻两个离线假设：① UMD `SetSyncPrim` 实际发 `0x2:0xa`（objdump 实锤 `mov $0xa,%edx`；Ghidra 伪 C 写错 fn id；真身是跳板）——r220 handler 挂错位置，下轮搬到 `0x2:0xa`；② check-only 翻译不等 UFO 值（value=1 vs PMR=0 一次通过，fence=3），源码系 `if (nupdate)` 门控（r174 引入，疑笔误），r212/r213 只证明机械不证明值匹配。详见 `reports/r221-nonzerokick-findings.md` + trace。
- 拆桥干净（probe 25→1），默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：SyncPrimSet 搬移 + `if (nupdate)` 修复各独立成轮；非零 kick 双腿复验待搬移后。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r220：SyncPrimSet 真写离线实现，零硬件触碰）

- 开工声明零硬件触碰。“打通卡点”落到值语义链真卡点：`0x2:0x2` 从 stub 改真写（wrapper/生成头/活体三重互证 IN16；复用 translator 解析 + `index*4` 定界 + host 写；无 fence/提交/wakeup）。`0x2:0xd` 仍越界。**注意（r221 修正）：真 setter 实为 `0x2:0xa`，handler 挂错位置待搬移。**
- 门禁 7 项新 + `ddk2_render2` 改判 + wire MAPPING 补两行（生成表 16/4 diff 通过）；双重反向验证；`check-offline` 317 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载，会话未碰。详见 `reports/r220-syncprimset-write.md`。
- 遗留：非零值 kick 活体待下轮批准窗口（重载 + raw set + kick 链）。USB 短页标题日期问题留待对应轮。

---
