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

最后更新：2026-10-07（r213 DDK2 check-only 新会话复验，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r213：DDK2 check-only 新会话复验，批准执行）

- r211 新会话上 DDK2 check-only 复验通过：桥以 `drm_major=2 translate_kick=1` 重载（probe 未碰），`0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全 `ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=2`（与 r149 逐字同形）。
- 首跑沿用 legacy 链形（CCB 传 `b5`）在 CCB create 后用户态 SIGSEGV——正是 r144 定论的 `b5*` 间接缺失；仅改该传参后即绿。桥侧干净回收，无内核异常。trace 249 行已入库 `reports/r213-ddk2checkonly-reverify.jsonl`。详见 `reports/r213-ddk2checkonly-reverify.md`。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**
- 遗留：真实绘制 CCB 仍待 DDK2 render backend 接线；update 语义活体待 `0x82:0x14` handler。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r212：check-only 新会话复验，批准执行）

- r211 新会话上 legacy check-only 复验通过：桥以 `translate_kick=1` 重载（probe 未碰），r73 配方首跑全绿（check 值直接用 r148 实测值 0），六符号全 0、`0x88:0x4 ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=1`（与 r148 同形）。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。trace 130 行已入库 `reports/r212-checkonly-reverify.jsonl`（不再放易失 `/tmp`）。详见 `reports/r212-checkonly-reverify.md`。
- **Freeze 已恢复**：不 rmmod、不 unbind、不提交额外工作。
- 遗留：DDK2 check-only（r149 对应项）仍待复验；真实绘制 CCB 仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
