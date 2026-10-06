# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r173 门控活体兑现；捕获待 `=2` 窗口）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r173：DDK 门控活体兑现）

- 真机已批准（本轮零重载、只读复盘 + 时间线对齐）。所谓 submit/unwind 双峰 = 桥参数窗口：`=2`→`0x89:0x8`→`0x89:0xa`（`-25`）；默认→legacy `0x89:0x0`（`-25`）→有序自拆。桥 0x89 组仅 `{0x5,0x6,0x8,0x9}`，`0x0/0x4/0xa` 皆 `-ENOTTY`——两路按设计拒收，无 UMD 随机选路。
- 会话健康 freeze 继续。证据：`reports/r173-ddk-gate-live.md` + unwind 全 trace。候选下一步：重开 `=2` 窗口捕获真实字节。

---

## 本次会话进展（r172：DDK2 重载与首个真实 Submit3）

- 真机已批准（两次桥重载均 ref0，probe 零触碰）。`=2` 下 DDK2 全链 123 调用零非零；真实 blit 发出首个真实 `0x89:0xa`（`0/2/0`、`0x8000f44000`/`0x1200`，桥回 `-25` 无 GPU 工作；VA/尺寸与 fabricated 一致）。真实 CCB 字节未捕获（Rss=0 之谜，下一轮）。桥已恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r172-ddk2-reload-real-submit.md` + 双 trace。候选下一步：真实字节捕获或 accept-and-log handler。

---

