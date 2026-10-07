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

## 本次会话进展（r247：translator 并发冲突，批准执行）

- 双混合进程并行一胜一败：败者混合 fire 59µs 即时 FAIL errno=16（EBUSY，submit 环节被拒，tag 被消费无行）；全局 markers 状态机不支持并发 submit。附带修 poll 假阳性（fire 失败跳过 poll）+ 门禁更新 + 反向；`check-offline` 327 Python OK。详见 `reports/r247-translator-contention.md`。
- 拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：tag=3/5 归属未全闭合（诚实记录）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。

## 本次会话进展（r246：DDK2 param_1 三候选证伪，批准执行）

- r228 后续形状搜索完成：`b14*`/`b5*`/conn 三候选 DDK2 下全崩；GDB 定三级链断裂点；语料确认外部导出。工具边界，非桥缺口。详见 `reports/r246-ddk2param-shape.md`。
- 拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**无代码改动。
---
