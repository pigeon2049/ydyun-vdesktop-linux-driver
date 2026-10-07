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

最后更新：2026-10-07（r232 DDK2 混合回归 + r233 多 update，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r233：多 update 条目，批准执行）

- translator update 循环多条目活体走通：update 数组 2 条目，混合 fire（check=2 + update=2）45.7ms 即过，改探第二槽 0.10ms 即过（全发布证实）；dmesg fence=13/14。门禁更新（update count=2）+ 双门禁反向；`check-offline` 325 Python OK。详见 `reports/r233-multi-update-live.md`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 本次会话进展（r232：translator 混合 DDK2 回归，批准执行）

- translator 与 major 正交证实：`=2` + `translate_kick=1` 下混合工具 8 项全 ok（45.1ms 同构），fence=11/12；拆桥干净，默认 + L3 全绿。无代码改动。详见 `reports/r232-major2-mixed-regression.md`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
