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

## 本次会话进展（r246：DDK2 param_1 三候选证伪，批准执行）

- r228 后续形状搜索完成：`b14*`/`b5*`/conn 三候选 DDK2 下全崩（同 RVA `0xa0b38`）；GDB 定三级指针链断裂点（第二跳是 VA 常量）；语料确认 SetSyncPrim 是外部导出（无内部调用者）。工具边界，非桥缺口。详见 `reports/r246-ddk2param-shape.md` + `.jsonl`。
- 拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**无代码改动。
- 遗留：DDK2 UMD 对象暴露（离线先行）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。

## 本次会话进展（r245：L4 legacy 部分，批准执行）

- rung7 compute 全过 exit 0；rung5/6/8 在 render create 处 standalone 6 连崩，GDB 下全过（r167 翻版，桥无罪）。refs 不变，L3 全绿。详见 `reports/r245-l4legacy-partial.md`。
- **Freeze 继续。**无代码改动。
---
