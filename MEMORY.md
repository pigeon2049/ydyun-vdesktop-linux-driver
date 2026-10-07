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

## 本轮进展（r290：UMD 驱动 fire 首绿，批准执行）

- 桥 fire 串行移植（离线）：handler 定位+调度，work 逐块执行；门禁改判 13 项 + 反向 + 366+292 全绿 + W=1 零警告。停桌面窗口 + 三开 + 真实 blit：UMD 矩形 1280×1024 `fired=1 chunks=21 verified=1 bad=0/1310720`，STATUS #1 真实绘制打通。
- 恢复曲折：脚本 grep 缺 `-a` 误判 TIMEOUT；桌面 2 分钟自重启致 rmmod 被拒；手动补恢复关账（probe 31→1，L3 全绿，桌面拉回）。窗口零新增 WARN。脚本两 bug 已修。
- 教训：停桌面窗口可靠上限约 90 秒；CCB nonzero=40（+1 未命名）。**Freeze 已恢复。**
- 遗留：TA/3D CCB 与 update 的 UMD 驱动验证（下个 90 秒窗口）。本地提交仍未 push。
---

