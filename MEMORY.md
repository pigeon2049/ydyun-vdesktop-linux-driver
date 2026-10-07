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
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](MEMORY-HISTORY-2026-10-08.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r280：fire 分块循环离线实现，零硬件触碰）

- 接盘盘内半成品（struct 数组化、submit/teardown 仍单 fence，构建已破坏）并收尾：全帧按 51 行/块拆 21 块（上限 64），复用 scratch 基址，21 fence 逐序等、验尾块内容；teardown 全放；中途失败全 put。
- 门禁：`test_pvr_tqx_fire.py` +3（切条/全等/全放），反向验证通过（首轮改宏名后缀因被子串包含未触发，作废记教训；删 `chunks=%u` 即 2 FAIL，还原即绿）。`check-offline` 354+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：分块 fired/verified 活体（需批准：`=2` + tqx_ctx + fire 三开 + 真实 blit，看 `chunks=21` + `verified=1`）。USB 短页标题日期问题留待对应轮。
---

## 本次会话进展（r279：64 页绕行验证，批准执行）

- Chrome 关闭后重载验证：`=2` + tqx_ctx 下 slices ready + tqx-ctx ready 全现（bind 在 64 页下通过，2112 黑盒绕过）；blit hanging 60s 被杀系 UMD 行为（无 D 态，可 rmmod，对称归零）。拆桥干净，默认 + L3 全绿；本轮窗口零 WARN。trace 已入库。详见 `reports/r279-space64-live.md` + `.jsonl`。
- **Freeze 已恢复。**无代码改动（r278 离线部分已在盘）。
- 遗留：fire 分块循环（离线）；分块 fired/verified 活体。USB 短页标题日期问题留待对应轮。
---
