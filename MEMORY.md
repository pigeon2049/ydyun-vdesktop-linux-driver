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

最后更新：2026-10-07（r203 GFX update fabricated 干净返回）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r203：GFX update fabricated 干净返回）

- 零硬件触碰。GDB watchpoint 证明 `RGXKickGfx` 在 RVA `0x7ee1a` 将 0x408 字节复制到仅 0x80 bytes 的 b24，覆盖 update-list chunk size（`0x91→0x1151`），造成 r201 free abort。b24/b25 扩为 0x410 后 header 完整，`0x82:0x14` 发出且 RGXKickGfx 返回 0、进程正常退出。
- 证据：`reports/r203-gfx-update-clean.md` + trace。下一步离线核对并补齐 bridge `0x82:0x14` handler ABI；真实 CCB 仍冻结待批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r202：update-list 生命周期核对）

- 零硬件触碰。`FUN_00178800` 按 count 分配 update-list 并复制条目；r203 动态定位覆盖 update-list chunk header 的输出越界并在扩大 harness 缓冲后消除 abort。
- 证据：`reports/r202-update-list-lifetime.md`。后续动态复核见 r203。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
