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

最后更新：2026-10-07（r214 真实绘制第二样本，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r214：真实绘制第二样本，批准执行）

- r211 新会话上真实 `musa_blit_test -device 0 -f -o`（`=2` 桥，shim 仅 passthrough 记录）发出单次 `0x89:0xa`（8201 行 trace，与 r174 次轮同行数）；observe 行 VA/尺寸/res/PMR/39B/首偏移与 r174 全同；39 非零字节 37 跨会话一致，仅 `+0x40` 取第三值 `33 57`。执行级比对（非推断）+ 源码核对 observe 回 0 无执行。
- UMD 随后用户态 SIGABRT（r172 同例），内核侧干净；拆桥干净，桥恢复默认 + L3 全绿（node + smoke），dmesg 零 WARNING/BUG/Oops。trace 已入库 `reports/r214-realblit-sample2.jsonl`。详见 `reports/r214-realblit-sample2.md`。
- **Freeze 已恢复。**
- 遗留：真实绘制执行仍待 backend 接线；update 语义活体待 `0x82:0x14` handler。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r213：DDK2 check-only 新会话复验，批准执行）

- r211 新会话上 DDK2 check-only 复验通过：桥以 `drm_major=2 translate_kick=1` 重载（probe 未碰），`0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全 `ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=2`（与 r149 逐字同形）。
- 首跑沿用 legacy 链形（CCB 传 `b5`）在 CCB create 后用户态 SIGSEGV——正是 r144 定论的 `b5*` 间接缺失；仅改该传参后即绿。桥侧干净回收，无内核异常。trace 249 行已入库 `reports/r213-ddk2checkonly-reverify.jsonl`。详见 `reports/r213-ddk2checkonly-reverify.md`。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**
- 遗留：真实绘制 CCB 仍待 DDK2 render backend 接线；update 语义活体待 `0x82:0x14` handler。USB 短页标题日期问题留待对应轮。

---
