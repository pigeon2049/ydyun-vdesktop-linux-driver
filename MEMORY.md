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

最后更新：2026-10-07（r234 DDK2 双腿 + r235 开关回归，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r235：observer 在 translator 桥下回归，批准执行）

- observer 与 `translate_kick` 开关正交证实：同窗口 22 步全过，CCB 行一致；拆桥干净，默认 + L3 全绿。无代码改动。详见 `reports/r235-ck-observe-regression.md`。

## 本次会话进展（r234：DDK2 check 双腿，批准执行）

- `if (ncheck)` 在 DDK2 下同样真实：零值 0.046s 过 / 失配 5.005s 后 UMD 37；dmesg 仅匹配腿落 translated 行（fence=15）。双 trace 已入库。详见 `reports/r234-ddk2check-legs.md` + 双 `.jsonl`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
