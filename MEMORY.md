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

## 本次会话进展（r237：多文件并发，批准执行）

- 桥 per-file 隔离活体证实：默认桥上双 ping 并行双 PASS；dmesg 6 行齐、各自独立句柄，同 VA 零串扰。事后 refs 1/0，L3 全绿。首跑两次路径弯路（如实记录）。详见 `reports/r237-concurrent-live.md`。
- **Freeze 继续。**无代码改动。

## 本次会话进展（r236：fence fd poll，批准执行）

- translator fence 语义活体证实：混合 fire 回 0 后 poll 即时就绪；fence=16/17。工具加 poll 断言 + 门禁 +1（含一次弱反向后的强反向）；`check-offline` 326 Python OK。详见 `reports/r236-fence-poll-live.md`。
- 活体跑在 `=2`+translate_kick 在载桥（未重载）；R_H 后默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend；DDK2 UMD 侧断点（r228）。USB 短页标题日期问题留待对应轮。
---
