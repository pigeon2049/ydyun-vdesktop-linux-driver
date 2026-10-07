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

最后更新：2026-10-07（r198 AppHint 初始化与 GFX 重放；r197 commit 948a5bc 已 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r198：无 poke AppHint 初始化并追到 SubmissionCmdGenerate）

- 零硬件触碰（fabricated）。同版 UMD 通过临时 `musa.ini` 将 `PerfCountEndCbID=0` 初始化到 render context；无对象内存 poke。GFX 越过 `RGXPrepareTA`，但在 `SubmissionCmdGenerate` 因首参为空 SIGSEGV；109 trace 行无 `0x82:0x14`。不宣称 update helper 已动态复验。
- 证据：`reports/r198-gfx-apphint-replay.md` + trace。新发现 `+0x24` AppHint 字段同时参与 PrepareTA context 状态表索引；update-list count 的 `+0x24` 属于另一个新分配对象。
- 下一步追 SubmissionCmdGenerate submission-context 首参来源，并用真实构造路径喂入；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r197：纠正 RGXKickGfx update 初始化解释）

- 零硬件触碰；离线 UMD SHA 与语料一致。r196 写零的 render-context `+0x20/+0x24` 是 PerfCountStart/EndCbID；r198 另证 `+0x24` 同时作为 PrepareTA 状态表索引。update list 是 RGXPrepareTA 单独分配并将新对象 `+0x24` 计数置零，再从调用者 psKickTA 复制条目。临时 musa.ini 已无 poke 越过 PrepareTA，但卡在空 submission context；动态 update helper 验证待做。
- 证据：`reports/r197-correct-gfx-update-init.md` + `reports/r198-gfx-apphint-replay.md`。下一步追 RGXKickGfx 的 SubmissionCmdGenerate 首参来源。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---
