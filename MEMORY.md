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

最后更新：2026-10-07（r197 纠正 update-list 初始化解释；r196 commit 8a332df 已 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r197：纠正 RGXKickGfx update 初始化解释）

- 零硬件触碰；离线 UMD SHA 与语料一致。r196 写零的 render-context `+0x20/+0x24` 是 PerfCountStart/EndCbID，不是 update slot；update list 是 RGXPrepareTA 内分配并将其 `+0x24` 计数置零，再从调用者 psKickTA 复制条目。静态路径待 fabricated 重放验证；r196 trace 到 `0x82:0x14` 有效，但“poke 是必要条件”撤回。
- 证据：`reports/r197-correct-gfx-update-init.md`。下一步保留 perf defaults，在 psKickTA 输入数组构造 update 后 GDB 观察 prepare list 和 bridge trace。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r196：RGXKickGfx update producer）

- 零硬件触碰（fabricated）。`RGXKickGfx` 到达 `SubmissionSetUpdateSyncPrim`，实测 count=1、首项 flag=2；trace 发出 `0x82:0x14`（IN 108/OUT 4），fake shim 返回 0，函数返回 0。r197 更正：之前手动清零的 render-context `+0x20/+0x24` 是 perf callback AppHint 字段；真实 update-list 初始化在 `RGXPrepareTA`，动态验证待做。
- 证据：`reports/r196-gfx-update-producer.md` + trace；r197 已更正 poke 字段解释。下一步保留 perf defaults，按 psKickTA 输入条目重放；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---
