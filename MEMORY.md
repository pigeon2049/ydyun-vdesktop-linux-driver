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

最后更新：2026-10-07（r199 GFX allocator 指针链定位；r198 commit 16c279f 已 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r199：追踪 GFX submission allocator 首参来源）

- 零硬件触碰；同 SHA UMD 二进制指令核对：RGXKickGfx 从 kick `+0x28` 所指对象的 `+0x200` 取 SubmissionCmdGenerate 首参。render-context 构造器在 context `+0x200` 建 SubmissionBufAlloctor；SubmissionHead 是另一个对象、作为第二参。r198 的空终值原因未动态区分。
- 证据：`reports/r199-gfx-submission-allocator-origin.md`。下一步 GDB 逐级读取 kick `+0x28`、目标 `+0x200`、kick `+0x2d8` 与真实 render-context `+0x200`，再按验证后的字段复放。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r198：无 poke AppHint 初始化并追到 SubmissionCmdGenerate）

- 零硬件触碰（fabricated）。同版 UMD 通过临时 `musa.ini` 将 `PerfCountEndCbID=0` 初始化到 render context；无对象内存 poke。GFX 越过 `RGXPrepareTA`，但在 `SubmissionCmdGenerate` 因首参为空 SIGSEGV；109 trace 行无 `0x82:0x14`。不宣称 update helper 已动态复验。
- 证据：`reports/r198-gfx-apphint-replay.md` + trace。新发现 `+0x24` AppHint 字段同时参与 PrepareTA context 状态表索引；update-list count 的 `+0x24` 属于另一个新分配对象。
- 下一步按 r199 指令核对结果，GDB 逐级确认 kick 输入和 allocator 对象是否对应；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---


---
