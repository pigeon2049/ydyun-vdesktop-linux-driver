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

最后更新：2026-10-07（r200 fabricated allocator 实测；r199 commit cca8550 已 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r200：fabricated render context allocator 实测）

- 零硬件触碰。fabricated 默认 shim 下 connect/device/devmemctx/render 全返 0；返回 context `+0x200` allocator 和 `+0x318` SubmissionHead 均非空，trace 117 行。尚未调用 RGXKickGfx；r198 的 kick `+0x28` 指向对象仍未核验。
- 证据：`reports/r200-renderctx-allocator.md` + `reports/r200-renderctx-allocator.jsonl`。下一步在 GFX call-site 断点逐级 dump 两级 allocator 链，再复放。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r199：追踪 GFX submission allocator 首参来源）

- 零硬件触碰；同 SHA UMD 二进制指令核对：RGXKickGfx 从 kick `+0x28` 所指对象的 `+0x200` 取 SubmissionCmdGenerate 首参。render-context 构造器在 context `+0x200` 建 SubmissionBufAlloctor；SubmissionHead 是另一个对象、作为第二参。r198 的空终值原因未动态区分。
- 证据：`reports/r199-gfx-submission-allocator-origin.md`。下一步 GDB 逐级读取 kick `+0x28`、目标 `+0x200`、kick `+0x2d8` 与真实 render-context `+0x200`，再按验证后的字段复放。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---


---


---
